#include <curl/curl.h>
#include <stdlib.h>
#include <cstring>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <condition_variable>
#include <vector>
#include <thread>
#include <iostream>
#include <sstream>
#include <regex>

#include "safetypes.hpp"
#include "logging.hpp"

#include <chrono>
#include <atomic>

const std::string WIKIPEDIA_DOMAIN = "https://en.wikipedia.org";

const unsigned num_pull_threads = 8;
const unsigned num_parse_threads = 4;

std::mutex final_notify;
std::atomic<bool> searching;
std::condition_variable notify_main_thread;

SafeQueue<std::vector<std::string>> pull_queue;
SafeQueue<std::pair<std::vector<std::string>, std::string>> parse_queue;
SafeSet<std::string> url_set;

std::string source_path = "/wiki/GitHub";
std::string dest_path =   "/wiki/Linus_Torvalds";

std::atomic<uint16_t> fetched;
std::atomic<uint16_t> parsed;

void print_vec(std::vector<std::string> v) {
    std::string s = "[ ";
    for(const auto& el : v) {
        s += el + " ";
    }
    log(s + "]");
}

void stop() {
    if(searching.load(std::memory_order_relaxed) == false)
        return;
    std::lock_guard<std::mutex> lk(final_notify);
    searching.store(false, std::memory_order_relaxed);
    notify_main_thread.notify_one();
    pull_queue.notify_all();
    parse_queue.notify_all();
}

size_t writedata(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::ostringstream *stream = (std::ostringstream*)userdata;
    size_t count = size*nmemb;
    stream->write(ptr, count);
    return count;
}
void pull() {
    while(searching) {
        std::ostringstream stream;
        CURL* curl;
        CURLcode res;

        std::vector<std::string> vec = pull_queue.wait_for_element();
        auto& s = vec.back();

retry:
        if(!searching)
            return;

        curl = curl_easy_init();
        if(!curl) {
            log("Unable to setup cURL");
            continue;
        }
        // llog(WIKIPEDIA_DOMAIN + s);
        curl_easy_setopt(curl, CURLOPT_URL, (WIKIPEDIA_DOMAIN + s).c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writedata);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);
        res = curl_easy_perform(curl);
        if(res) {
            log("While attempting to pull: " + WIKIPEDIA_DOMAIN + s, std::cerr);
            log("Code: " + std::to_string(res), std::cerr);
            log("Error: " + std::string(curl_easy_strerror(res)), std::cerr);
            goto retry;
        } else {
            parse_queue.push(std::pair<std::vector<std::string>, std::string>(vec, stream.str()));
        }
        curl_easy_cleanup(curl);
        fetched.fetch_add(1, std::memory_order_relaxed);
    }
}

// This regular expression only looks for /wiki pages and
// discards most Wikipedia meta pages (e.g. /wiki/Help: articles)
const std::regex ANCHOR_REGEX("<a[^>]*href=[\"'](\\/wiki\\/(?:(?!Wikipedia:)(?!File:)(?!Help:)(?!Template:))[\\w-%?/\\(\\)\\.=&:@;*'!\\[\\]#,\\$\\+]+)[\"'][^>]*>", std::regex_constants::icase | std::regex_constants::ECMAScript);
const std::regex PAGE_NAME_REGEX("\"wgPageName\":\"([\\w-%?/\\(\\)\\.=&:@;*'!\\[\\]#,\\$\\+]+)\"", std::regex_constants::icase | std::regex_constants::ECMAScript);
const std::string WIKI_STRING = "/wiki/";
void parse() {
    const auto matches_end = std::sregex_iterator();
    while(searching) {
        auto pair = parse_queue.wait_for_element();
        auto const& from = pair.first;
        auto const& body = pair.second;
        if(!searching)
            return;
        /* Check the pageName (in case of redirects) */
        if(std::equal(WIKI_STRING.begin(), WIKI_STRING.end(), from.back().begin())) { // normal wiki page
            auto matches_begin = std::sregex_iterator(body.begin(), body.end(), PAGE_NAME_REGEX);
            for(std::sregex_iterator i = matches_begin; i != matches_end; ++i) {
                std::smatch sm = *i;
                std::smatch::iterator pn_it = sm.begin();
                auto const dest_nowiki = dest_path.substr(6);
                for(std::advance(pn_it, 1); pn_it != sm.end(); std::advance(pn_it, 1)) {
                    const std::string s = *pn_it;
                    if(std::equal(dest_nowiki.begin(), dest_nowiki.end(), s.begin())) {
                        log("Found pageName match", std::cerr);
                        print_vec(from);
                        stop();
                        return;
                    }
                }
            }
        }

        /* Check all anchors */
        auto matches_begin  = std::sregex_iterator(body.begin(), body.end(), ANCHOR_REGEX);
        for(std::sregex_iterator i = matches_begin; i != matches_end; ++i) {
            std::smatch sm = *i;
            std::smatch::iterator it = sm.begin();
            for(std::advance(it, 1); it != sm.end(); std::advance(it, 1)) {
                auto from_copy = from;
                if(*it == dest_path) {
                    from_copy.emplace_back(*it);
                    print_vec(from_copy);
                    stop();
                    return;
                }
                if(url_set.if_not_contains_add(*it)) {
                    from_copy.emplace_back(*it);
                    pull_queue.push(from_copy);
                }
            }
        }
        parsed.fetch_add(1, std::memory_order_relaxed);
    }
}

int handle(int argc, char** argv) {
    switch(argc) {
        case 3:
            source_path = std::string(argv[1]);
            dest_path = std::string(argv[2]);
            std::cout << "Source path:      " << source_path << std::endl;
            std::cout << "Destination path: " << dest_path << std::endl;
        case 1:
            return 0;
        case 2:
            std::cout << "Not enough arguments specified" << std::endl;
            return 1;
        default:
            std::cout << "Too many arguments specified" << std::endl;
            return 1;
    }
}

void info(void) {
    fetched = 0;
    parsed = 0;
    {
        std::lock_guard<std::mutex> lk(io_mutex);
        std::cout << "Parsed links per second:  working...\n"
                  << "Fetched links per second: working...\n"
                  << "Number of known pages:    working...\n"
                  << "Size of pull queue:       working...\n"
                  << "Size of parse queue:      working...\n";
    }
    while(true) {
        if(!searching) return;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if(!searching) return;
        auto fetched_copy = fetched.exchange(0, std::memory_order_relaxed);
        auto parsed_copy = parsed.exchange(0, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(io_mutex);
        std::cout << "\033[F\033[F\033[F\033[F\033[F";
        std::cout << "Parsed links per second:    " << parsed_copy << "               \n"
                  << "Fetched links per second:   " << fetched_copy << "              \n"
                  << "Number of known pages:      " << url_set.size() << "            \n"
                  << "Size of pull queue:         " << pull_queue.size() << "         \n"
                  << "Size of parse queue:        " << parse_queue.size() << "        \n";
    }
}

int main(int argc, char** argv) {

    if(handle(argc, argv))
        return 1;

    searching.store(true, std::memory_order_relaxed);

    std::vector<std::thread> pull_threads;
    for(unsigned i = 0; i < num_pull_threads; ++i)
        pull_threads.emplace_back(pull);

    std::vector<std::thread> parse_threads;
    for(unsigned i = 0; i < num_parse_threads; ++i)
        parse_threads.emplace_back(parse);

    std::thread info_thread(info);

    pull_queue.push(std::vector<std::string>{source_path});

    std::unique_lock<std::mutex> lk(final_notify);
    notify_main_thread.wait(lk, []{
        return !searching; // keep waiting if we're still searching
    });

    for(unsigned i = 0; i < num_pull_threads; ++i)
        pull_threads[i].join();
    for(unsigned i = 0; i < num_parse_threads; ++i)
        parse_threads[i].join();
    info_thread.join();

    return 0;

}
