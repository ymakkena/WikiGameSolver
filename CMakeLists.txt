cmake_minimum_required(VERSION 2.8)
project(wikipedia)

find_package(CURL REQUIRED)
find_package(Threads)

set (CMAKE_CXX_STANDARD 14)

if(MSVC)
	# Visual Studio -- /W4
	if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif()
elseif(CMAKE_COMPILER_IS_GNUXX OR CMAKE_COMPILER_IS_GNUCC)
	# GCC -- -Wall -pedantic
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-implicit-fallthrough")
endif()

add_executable(wikipedia wikipedia.cpp)
target_include_directories( wikipedia PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(wikipedia ${CURL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
