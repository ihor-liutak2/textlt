#include "remote/remote_http_client.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    using textlt::RemoteHttpClient;

    std::cerr << "Unreserved URL characters\n";
    assert(RemoteHttpClient::UrlEncode("abcXYZ012-_.~") == "abcXYZ012-_.~");
    assert(RemoteHttpClient::UrlEncode("a b") == "a%20b");
    std::cerr << "Reserved URL characters\n";
    assert(RemoteHttpClient::UrlEncode("x/y?q=1&n=2") == "x%2Fy%3Fq%3D1%26n%3D2");
    std::cerr << "UTF-8 URL encoding\n";
    assert(RemoteHttpClient::UrlEncode("привіт").find('%') != std::string::npos);

    std::cout << "remote_http_client_test passed\n";
    return 0;
}
