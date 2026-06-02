/*
 * Copyright (c) 2013 SURFnet bv
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SOFTHSM_V2_GENERATIONTESTS_H
#define _SOFTHSM_V2_GENERATIONTESTS_H

#include <cppunit/extensions/HelperMacros.h>

class GenerationTests : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(GenerationTests);
	CPPUNIT_TEST(testObjectCacheDisabled);
	CPPUNIT_TEST(testObjectCacheEnabled);
	CPPUNIT_TEST(testTokenCacheDisabled);
	CPPUNIT_TEST(testTokenCacheEnabled);
	CPPUNIT_TEST_SUITE_END();

public:
	void testObjectCacheDisabled();
	void testObjectCacheEnabled();
	void testTokenCacheDisabled();
	void testTokenCacheEnabled();

	void setUp();
	void tearDown();
};

#endif // !_SOFTHSM_V2_GENERATIONTESTS_H
