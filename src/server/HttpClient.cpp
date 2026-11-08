/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "HttpClient.h"

#include <Url.h>

#include <private/netservices2/ExclusiveBorrow.h>
#include <private/netservices2/HttpFields.h>
#include <private/netservices2/HttpRequest.h>
#include <private/netservices2/HttpResult.h>
#include <private/netservices2/HttpSession.h>
#include <private/netservices2/NetServicesDefs.h>

#include <DataIO.h>

#if __has_include(<sen/SenSocketHttp.h>)
#	include <sen/SenSocketHttp.h>
#	define BASHO_SOCKET_FALLBACK 1
#endif

#include <string_view>

using namespace std::literals;
using namespace BPrivate::Network;

namespace basho {

static const char* const kUserAgent = "Basho/0.1 (location server for Haiku)";

// The session of the process, made when it is first needed and never deleted: Haiku's BHttpSession crashes with a double free when it is deleted right after an answer was read
// (see sensei/common/SharedHttpSession.h). The server runs until it is quit, and BHttpSession can be used from several threads.
static BHttpSession*
Session()
{
	static BHttpSession* session = new BHttpSession();
	return session;
}


#ifdef BASHO_SOCKET_FALLBACK
static bool
GetOverSocket(const std::string& url, std::string* body, std::string* error, int timeoutMs)
{
	int httpStatus = 0;
	status_t result = sen::http::SocketGet(url, kUserAgent, "application/json", body, &httpStatus, (bigtime_t)timeoutMs * 1000);
	if (result == B_OK)
		return true;
	*error = httpStatus != 0 ? "HTTP status " + std::to_string(httpStatus) : std::string(strerror(result));
	return false;
}
#endif


bool
HttpFetch(const std::string& url, std::string* body, std::string* error, int timeoutMs)
{
	body->clear();
	BUrl httpUrl(url.c_str(), true);
	if (!httpUrl.IsValid()) {
		*error = "not an address";
		return false;
	}

	auto request = BHttpRequest(httpUrl);
	request.SetTimeout((bigtime_t)timeoutMs * 1000);
	request.SetMaxRedirections(3);
	auto fields = request.Fields();
	fields.AddField("User-Agent"sv, kUserAgent);
	fields.AddField("Accept"sv, "application/json"sv);
	// (Fields() is a const reference and `fields` a copy: the headers are in the request only after this)
	request.SetFields(fields);

	auto buffer = make_exclusive_borrow<BMallocIO>();
	BHttpStatus status;
	try {
		auto result = Session()->Execute(std::move(request), BBorrow<BDataIO>(buffer));
		// (Status() blocks until the whole answer has been received)
		status = result.Status();
		result.Body();	// synchronize with the borrowed buffer
	} catch (const BNetworkRequestError& err) {
		*error = std::string("network error ") + strerror(err.ErrorCode());
		return false;
	} catch (const BError& err) {
#ifdef BASHO_SOCKET_FALLBACK
		return GetOverSocket(url, body, error, timeoutMs);
#else
		*error = std::string("the answer could not be read: ") + err.Message();
		return false;
#endif
	}

	if (status.code < 200 || status.code >= 300) {
		*error = "HTTP status " + std::to_string(status.code);
		return false;
	}
	try {
		body->assign(reinterpret_cast<const char*>(buffer->Buffer()), buffer->BufferLength());
	} catch (const BBorrowError&) {
		*error = "the answer could not be read";
		return false;
	}
	return true;
}

}	// namespace basho
