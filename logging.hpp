#ifndef LOGGING_H

#define LOGGING_H

#include <mutex>
#include <iostream>
#include <ostream>

std::mutex io_mutex;

void log(std::string message, std::ostream& out=std::cout)
{
    std::lock_guard<std::mutex> lock(io_mutex);
    out << message << std::endl;
}

#endif // LOGGING_H
