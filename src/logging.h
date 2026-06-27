#pragma once

#include <string>

enum class LogLevel { Info, Verbose, Error };

void logSetVerbose(bool verbose);
void logSetJson(bool json);
bool logIsJson();

void logMessage(LogLevel level, const std::string &message);
void logJsonEvent(const std::string &event,
                  const std::string &extraFields = {});

std::string formatHresult(long hr);
std::string wideToUtf8(const std::wstring &wide);
