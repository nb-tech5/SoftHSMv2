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
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*****************************************************************************
 GenerationTests.cpp

 Test cases for the Generation class cache behaviour.  Both the object-level
 and token-level caches are covered; each has its own set of globals in
 Generation.cpp so they are fully segregated.
 *****************************************************************************/

#include "config.h"
#include "GenerationTests.h"
#include "Generation.h"
#include "File.h"

#include <chrono>
#include <mutex>

// Access the module-level cache globals from Generation.cpp directly so tests
// can seed and inspect state without depending on environment variables.
extern bool cacheObjectGeneration;
extern std::chrono::milliseconds cacheObjectGenerationPeriodMs;
extern std::chrono::milliseconds cacheObjectGenerationLastUpdate;

extern bool cacheTokenGeneration;
extern std::chrono::milliseconds cacheTokenGenerationPeriodMs;
extern std::chrono::milliseconds cacheTokenGenerationLastUpdate;

CPPUNIT_TEST_SUITE_REGISTRATION(GenerationTests);

static const char* OBJ_PATH =
#ifndef _WIN32
	"testdir/gen.object";
#else
	"testdir\\gen.object";
#endif

static const char* TOK_GEN_PATH =
#ifndef _WIN32
	"testdir/generation";
#else
	"testdir\\generation";
#endif

// Write a single unsigned-long generation counter to a file, creating/truncating it.
static void writeGenFile(const char* path, unsigned long value)
{
	File f(path, DEFAULT_UMASK, false, true, true, true);
	CPPUNIT_ASSERT(f.isValid());
	CPPUNIT_ASSERT(f.writeULong(value));
}

void GenerationTests::setUp()
{
#ifndef _WIN32
	CPPUNIT_ASSERT(!system("mkdir testdir"));
#else
	system("mkdir testdir 2> nul");
#endif
	// Reset both cache sets to a well-known, disabled state before every test.
	cacheObjectGeneration = false;
	cacheObjectGenerationLastUpdate = std::chrono::milliseconds(0);
	cacheObjectGenerationPeriodMs = std::chrono::milliseconds(1000);

	cacheTokenGeneration = false;
	cacheTokenGenerationLastUpdate = std::chrono::milliseconds(0);
	cacheTokenGenerationPeriodMs = std::chrono::milliseconds(1000);
}

void GenerationTests::tearDown()
{
	cacheObjectGeneration = false;
	cacheObjectGenerationLastUpdate = std::chrono::milliseconds(0);
	cacheObjectGenerationPeriodMs = std::chrono::milliseconds(1000);

	cacheTokenGeneration = false;
	cacheTokenGenerationLastUpdate = std::chrono::milliseconds(0);
	cacheTokenGenerationPeriodMs = std::chrono::milliseconds(1000);

#ifndef _WIN32
	CPPUNIT_ASSERT(!system("rm -rf testdir"));
#else
	CPPUNIT_ASSERT(!system("rmdir /s /q testdir 2> nul"));
#endif
}

// With cacheObjectGeneration=false every wasUpdated() call reads from disk, so
// an externally written change is visible immediately on the next call.
void GenerationTests::testObjectCacheDisabled()
{
	cacheObjectGeneration = false;

	writeGenFile(OBJ_PATH, 1UL);

	Generation* gen = Generation::create(OBJ_PATH, DEFAULT_UMASK, false);
	CPPUNIT_ASSERT(gen != NULL);
	gen->set(1UL);

	// No change yet: disk==currentValue.
	CPPUNIT_ASSERT(!gen->wasUpdated());

	// Externally advance the on-disk generation counter.
	writeGenFile(OBJ_PATH, 2UL);

	// Without cache the update is detected immediately.
	CPPUNIT_ASSERT(gen->wasUpdated());

	gen->set(2UL);

	// currentValue is now 2; no further change → false.
	CPPUNIT_ASSERT(!gen->wasUpdated());

	delete gen;
}

// With cacheObjectGeneration=true the second wasUpdated() call within the
// cache window returns false even though the on-disk generation advanced,
// because the disk read is skipped entirely.
void GenerationTests::testObjectCacheEnabled()
{
	cacheObjectGeneration = true;
	cacheObjectGenerationPeriodMs = std::chrono::milliseconds(10000); // 10 s window
	cacheObjectGenerationLastUpdate = std::chrono::milliseconds(0);   // force first call to hit disk

	writeGenFile(OBJ_PATH, 1UL);

	Generation* gen = Generation::create(OBJ_PATH, DEFAULT_UMASK, false);
	CPPUNIT_ASSERT(gen != NULL);
	gen->set(1UL);

	// First call: cache miss (LastUpdate==0), reads disk (1==1 → no change),
	// stamps cacheObjectGenerationLastUpdate.
	CPPUNIT_ASSERT(!gen->wasUpdated());

	// Externally advance the on-disk generation while the cache is warm.
	writeGenFile(OBJ_PATH, 2UL);

	// Second call within the window: disk skipped → returns false (stale).
	// This is the intended trade-off: reduced I/O within the configured period.
	CPPUNIT_ASSERT(!gen->wasUpdated());

	delete gen;
}

// With cacheTokenGeneration=false every wasUpdated() call for a token
// generation reads from disk regardless of the object cache state.
void GenerationTests::testTokenCacheDisabled()
{
	// Warm the object-level cache to confirm the two caches are segregated.
	cacheObjectGeneration = true;
	cacheObjectGenerationPeriodMs = std::chrono::milliseconds(10000);
	cacheObjectGenerationLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch());

	cacheTokenGeneration = false; // token cache is off

	// create() calls commit() which writes currentValue=1 to the file.
	Generation* gen = Generation::create(TOK_GEN_PATH, DEFAULT_UMASK, true);
	CPPUNIT_ASSERT(gen != NULL);

	// Externally advance the on-disk token generation to 2.
	writeGenFile(TOK_GEN_PATH, 2UL);

	// Even though the object cache is warm, the token path reads from disk
	// and detects the change.
	CPPUNIT_ASSERT(gen->wasUpdated());

	delete gen;
}

// With cacheTokenGeneration=true the second wasUpdated() call for a token
// within the cache window skips the disk read, mirroring the object cache
// behaviour but using its own independent set of globals.
void GenerationTests::testTokenCacheEnabled()
{
	// Keep the object cache disabled to confirm the two caches are segregated.
	cacheObjectGeneration = false;

	cacheTokenGeneration = true;
	cacheTokenGenerationPeriodMs = std::chrono::milliseconds(10000); // 10 s window
	cacheTokenGenerationLastUpdate = std::chrono::milliseconds(0);   // force first call to hit disk

	// create() calls commit() which writes currentValue=1 to the file.
	Generation* gen = Generation::create(TOK_GEN_PATH, DEFAULT_UMASK, true);
	CPPUNIT_ASSERT(gen != NULL);

	// First call: cache miss (LastUpdate==0), reads disk (1==1 → no change),
	// stamps cacheTokenGenerationLastUpdate.
	CPPUNIT_ASSERT(!gen->wasUpdated());

	// Externally advance the on-disk token generation to 2 while cache is warm.
	writeGenFile(TOK_GEN_PATH, 2UL);

	// Second call within the window: disk skipped → returns false (stale).
	CPPUNIT_ASSERT(!gen->wasUpdated());

	delete gen;
}
