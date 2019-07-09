#pragma once
#include <string>
#include <functional>

#define  IN
#define  OUT

typedef std::function<void(IN const std::string&)> OnRead;
