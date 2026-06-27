#pragma once

#include <functional>
#include <string>


enum class LogLevel { Info, Verbose, Error };

using LogGuiSink =
    std::function<void(LogLevel level, const std::string &message)>;

void initConsoleEncoding();
void writeStdout(const std::wstring &text);
void writeStderr(const std::wstring &text);

void logSetVerbose(bool verbose);
void logSetJson(bool json);
bool logIsJson();
void logSetGuiSink(LogGuiSink sink);

void logMessage(LogLevel level, const std::string &message);
void logJsonEvent(const std::string &event,
                  const std::string &extraFields = {});

std::string formatHresult(long hr);
std::string wideToUtf8(const std::wstring &wide);
