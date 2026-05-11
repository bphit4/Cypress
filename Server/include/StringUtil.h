#pragma once
#include <string.h>

typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned long ulong;

static const char* extractFileName(const char* inStr)
{
	for (const char* charIter = inStr + int(strlen(inStr)) - 1; charIter >= inStr; --charIter)
	{
		if ((*charIter == '/') || (*charIter == '\\'))
		{
			return charIter + 1;
		}
	}
	return inStr;
}

static inline uint fnvHashWithSeed(const char* theStr, uint theHash)
{
	uint aChar;
	const uchar* aStr = reinterpret_cast<const uchar*>(theStr);

	while ((aChar = *aStr++))
		theHash = theHash * 33 ^ aChar;

	return theHash;
}

static inline uint fnvHash(const char* theStr)
{
	return fnvHashWithSeed(theStr, 5381);
}

static constexpr int fnvHashConstexpr(const char* theStr, uint theHash = 5381)
{
	return !*theStr ? theHash : fnvHashConstexpr(theStr + 1, uint(theHash * ulong(33) ^ *theStr));
}

static const char* cutPath(const char* path) {
	return strrchr(path, '/') + 1;
}

static std::vector<std::string> splitString(const std::string& str, char delimiter) 
{
	std::vector<std::string> tokens;
	std::istringstream iss(str);
	std::string token;
	while (std::getline(iss, token, delimiter)) {
		tokens.push_back(token);
	}
	return tokens;
}