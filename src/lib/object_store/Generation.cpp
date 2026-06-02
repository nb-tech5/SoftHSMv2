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
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*****************************************************************************
 Generation.cpp

 Helper for generation number handling.
 *****************************************************************************/

#include "config.h"
#include "log.h"
#include "Generation.h"

#include <mutex>
#include <chrono>
#include <stdexcept>
#include <sstream>

// Factory
Generation* Generation::create(const std::string path, int umask, bool isToken)
{
	Generation* gen = new Generation(path, umask, isToken);
	if ((gen != NULL) && isToken && (gen->genMutex == NULL))
	{
		delete gen;

		return NULL;
	}
	return gen;
}

// Destructor
Generation::~Generation()
{
	if (isToken)
	{
		MutexFactory::i()->recycleMutex(genMutex);
	}
}

// Synchronize from locked disk file
bool Generation::sync(File &objectFile)
{
	if (isToken)
	{
		ERROR_MSG("Generation sync() called for a token");

		return false;
	}

	unsigned long onDisk;

	if (!objectFile.readULong(onDisk))
	{
		if (objectFile.isEOF())
		{
			onDisk = 0;
		}
		else
		{
			return false;
		}
	}

	currentValue = onDisk;

	return objectFile.seek(0L);
}

std::string getenv_string(const char *name, const char *defaultValue /* = NULL */)
{
#ifdef _WIN32
	size_t valueSize = 0;
	char buffer[256];
	getenv_s(&valueSize, buffer, 256, name);
	buffer[valueSize] = '\0';
	const char *pPath = buffer;
#else
	const char *pPath = getenv(name);
#endif
	if (pPath == nullptr || pPath[0] == '\0')
	{
		if(defaultValue == NULL)
		{
			std::stringstream msg("Missing env var: ");
			msg << name;
			throw std::logic_error(msg.str());
		}
		else
		{
			return std::string(defaultValue);
		}
	}
	return std::string(pPath);
}

long stringToLong(const std::string value)
{
	std::istringstream str(value);
	long result;
	str >> result;
	if (!str) {
		if (result == std::numeric_limits<int>::max()) {
			throw std::logic_error("Overflow!");
		} else if (result == std::numeric_limits<int>::min()) {
			throw std::logic_error("Underflow!");
		} else {
			throw std::logic_error("Some other parse error");
		}
	}
	return result;
}

bool cacheTokenGeneration = getenv_string("SOFTHSM2_TOKEN_GENERATION_CACHE_ENABLE", "false") == "true";
std::chrono::milliseconds cacheTokenGenerationPeriodMs(stringToLong(getenv_string("SOFTHSM2_TOKEN_GENERATION_CACHE_PERIOD", "1000")));
std::chrono::milliseconds cacheTokenGenerationLastUpdate(0);
std::mutex cacheTokenGenerationMutex;

bool cacheObjectGeneration = getenv_string("SOFTHSM2_OBJECT_GENERATION_CACHE_ENABLE", "false") == "true";
std::chrono::milliseconds cacheObjectGenerationPeriodMs(stringToLong(getenv_string("SOFTHSM2_OBJECT_GENERATION_CACHE_PERIOD", "1000")));
std::chrono::milliseconds cacheObjectGenerationLastUpdate(0);
std::mutex cacheObjectGenerationMutex;

// Check if the target was updated
bool Generation::wasUpdated()
{
	if (isToken)
	{
		if (cacheTokenGeneration) {
			std::lock_guard<std::mutex> lock(cacheTokenGenerationMutex);
			if (cacheTokenGenerationLastUpdate > std::chrono::milliseconds(0) &&
				(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()) - cacheTokenGenerationLastUpdate) < cacheTokenGenerationPeriodMs)
			{
				DEBUG_MSG("Token generation cache hit, skipping disk read");
				return false;
			}
		}

		MutexLocker lock(genMutex);

		File genFile(path, umask);

		if (!genFile.isValid())
		{
			return true;
		}

		genFile.lock();

		unsigned long onDisk;

		if (!genFile.readULong(onDisk))
		{
			return true;
		}

		if (cacheTokenGeneration) {
			std::lock_guard<std::mutex> lock(cacheTokenGenerationMutex);
			cacheTokenGenerationLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch());
			DEBUG_MSG("Token generation cache miss, read from disk, cached until");
		}

		if (onDisk != currentValue)
		{
			currentValue = onDisk;
			return true;
		}

		return false;
	}
	else
	{
		if (cacheObjectGeneration) {
			std::lock_guard<std::mutex> lock(cacheObjectGenerationMutex);
			if (cacheObjectGenerationLastUpdate > std::chrono::milliseconds(0) &&
				(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()) - cacheObjectGenerationLastUpdate) < cacheObjectGenerationPeriodMs)
			{
				DEBUG_MSG("Object generation cache hit, skipping disk read");
				return false;
			}
		}
		File objectFile(path, umask);

		if (!objectFile.isValid())
		{
			return true;
		}

		objectFile.lock();

		unsigned long onDisk;

		if (!objectFile.readULong(onDisk))
		{
			return true;
		}

		if (cacheObjectGeneration) {
			std::lock_guard<std::mutex> lock(cacheObjectGenerationMutex);
			cacheObjectGenerationLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch());
			DEBUG_MSG("Object generation cache miss, read from disk");
		}
		return (onDisk != currentValue);
	}
}

// Update
void Generation::update()
{
	pendingUpdate = true;
}

// Commit
void Generation::commit()
{
	if (isToken)
	{
		MutexLocker lock(genMutex);

		File genFile(path, umask, true, true, true, false);

		if (!genFile.isValid())
		{
			return;
		}

		genFile.lock();

		if (genFile.isEmpty())
		{
			currentValue++;

			if (currentValue == 0)
			{
				currentValue++;
			}

			pendingUpdate = false;

			(void) genFile.writeULong(currentValue);

			genFile.unlock();

			return;
		}

		unsigned long onDisk = 0;

		bool bOK = true;

		bOK = bOK && genFile.readULong(onDisk);
		bOK = bOK && genFile.seek(0L);

		if (pendingUpdate)
		{
			onDisk++;

			if (onDisk == 0)
			{
				onDisk++;
			}
		}

		bOK = bOK && genFile.writeULong(onDisk);

		if (bOK)
		{
			currentValue = onDisk;

			pendingUpdate = false;
		}

		genFile.unlock();
	}
}

// Set the current value when read from disk
void Generation::set(unsigned long onDisk)
{
	currentValue = onDisk;
}

// Return new value
unsigned long Generation::get()
{
	pendingUpdate = false;

	currentValue++;

	if (currentValue == 0)
	{
		currentValue = 1;
	}

	return currentValue;
}

// Rollback (called when the new value failed to be written)
void Generation::rollback()
{
	pendingUpdate = true;

	if (currentValue != 1)
	{
		currentValue--;
	}
}

// Constructor
Generation::Generation(const std::string inPath, int inUmask, bool inIsToken)
{
	path = inPath;
	umask = inUmask;
	isToken = inIsToken;
	pendingUpdate = false;
	currentValue = 0;
	genMutex = NULL;

	if (isToken)
	{
		genMutex = MutexFactory::i()->getMutex();

		if (genMutex != NULL)
		{
			commit();
		}
	}
}
