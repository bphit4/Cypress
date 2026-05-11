#pragma once

void CypressAssert(const char* condition, const char* filename, int lineNumber, const char* msg, ...);

#ifdef _DEBUG
#define CYPRESS_ASSERT(condition, msg, ...) \
{ \
if(!bool(condition)) { CypressAssert(""#condition, __FILE__, __LINE__, msg, ##__VA_ARGS__); \
if(IsDebuggerPresent()) {DebugBreak();}} \
} 
#else
#define CYPRESS_ASSERT(condition, msg, ...) \
{ \
if(!bool(condition)) {CypressAssert(""#condition, nullptr, -1, msg, ##__VA_ARGS__);} \
}
#endif