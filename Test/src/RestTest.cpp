

#include "inc/Test.h"

#include <cpprest/json.h>
#include <cpprest/http_listener.h>
#include <cpprest/uri.h>
#include <cpprest/asyncrt_utils.h>


using namespace web;
using namespace http;
using namespace utility;
using namespace http::experimental::listener;


BOOST_AUTO_TEST_SUITE(RestTest)

BOOST_AUTO_TEST_CASE(RestTest)
{
    http_listener listener("http://localhost:8967/my/");
    listener.support([](http_request r){
        r.reply(status_codes::OK, json::value("abc"));
    });
    listener.open().wait();

    std::cout << utility::string_t(U("Listening for requests")) << std::endl;


    std::string line;
    std::getline(std::cin, line);

    listener.close().wait();
    std::cout << "REST OK!" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()