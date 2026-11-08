/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#ifndef _BASHO_HTTP_CLIENT_H_
#define _BASHO_HTTP_CLIENT_H_

#include <string>

namespace basho {

/**
 * GET with the network kit of Haiku (BHttpSession and BHttpRequest of libnetservices2), the way the HTTP classes of SEN do it (sensei/common/BaseEnricher.cpp): one session for the
 * process that is never deleted, the headers set with SetFields(), the body read through a borrowed BMallocIO, the errors of the kit caught.
 *
 * Haiku's client throws an exception that ends the program for an answer that has a header with an empty value (RFC 9110 allows one). If SEN's socket client (`<sen/SenSocketHttp.h>`,
 * header only, found at build time) is there, the request is made again over a socket when that happens; without the header the error is returned.
 *
 * `timeoutMs`: how long to wait for the answer. True: the body of a 2xx answer; false: `error` says why.
 */
bool	HttpFetch(const std::string& url, std::string* body, std::string* error, int timeoutMs = 10000);

}	// namespace basho

#endif
