#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

#include "mfsync/file_handler.h"

TEST_CASE("storage test", "[file_handler]") {
    auto handler = mfsync::file_handler();
    const auto stored = handler.get_stored_files();
    REQUIRE(stored.empty());

    handler.init_storage("data");
    const auto stored_new = handler.get_stored_files();
    REQUIRE(stored_new.size() == 2);

    mfsync::file_information some_file_info;
    for (const auto& stored_file : stored_new) {
        REQUIRE(handler.is_stored(stored_file));

        // change filename
        some_file_info = stored_file;
        some_file_info.file_name = "NON EXISTING";
        REQUIRE(!handler.is_stored(some_file_info));

        // change shasum
        some_file_info = stored_file;
        some_file_info.sha256sum = "NON EXISTING";
        REQUIRE(handler.is_stored(some_file_info));
    }

    REQUIRE(handler.is_stored("NOT STORED") == false);

    auto available = mfsync::available_file{
        some_file_info, boost::asio::ip::make_address("8.23.42.17"), 1337};

    handler.add_available_file(available);
    REQUIRE(!handler.is_available(available.file_info.file_name));
}
