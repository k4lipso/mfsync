#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

#include "mfsync/file_handler.h"
#include "mfsync/protocol.h"

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

TEST_CASE("broken single message deserialization", "[protocol") {
  const auto empty = mfsync::protocol::get_requested_file_from_message("");
  REQUIRE(!empty.has_value());

  const auto msg = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                 + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty2 =
      mfsync::protocol::get_requested_file_from_message(msg);
  REQUIRE(!empty2.has_value());

  const auto msg2 = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                  + std::string("{}")
                  + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty3 =
      mfsync::protocol::get_requested_file_from_message(msg2);
  REQUIRE(!empty3.has_value());

  const auto msg3 = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                  + std::string("{ \"foo\": \"bar\", \"baz\": 23 }")
                  + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty4 =
      mfsync::protocol::get_requested_file_from_message(msg3);
  REQUIRE(!empty4.has_value());
}

TEST_CASE("broken multi message deserialization") {
  const auto empty = mfsync::protocol::get_available_files_from_message("", {});
  REQUIRE(!empty.has_value());

  const auto msg = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                 + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty2 =
      mfsync::protocol::get_available_files_from_message(msg, {});
  REQUIRE(!empty2.has_value());

  const auto msg2 = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                  + std::string("{}")
                  + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty3 =
      mfsync::protocol::get_available_files_from_message(msg2, {});
  REQUIRE(!empty3.has_value());

  const auto msg3 = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                  + std::string("{ \"foo\": \"bar\", \"baz\": 23 }")
                  + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty4 =
      mfsync::protocol::get_available_files_from_message(msg3, {});
  REQUIRE(!empty4.has_value());
}

TEST_CASE("single message serialization", "[protocol") {
  for(size_t i = 0; i < 100; ++i)
  {
    const std::string name = "file" + std::to_string(i);
    const mfsync::file_information info{ name, std::nullopt, i};
    const mfsync::requested_file requested{ info, i, static_cast<unsigned>(i) };
    const auto msg =
        mfsync::protocol::create_message_from_requested_file(requested);
    const auto deserialized_request =
        mfsync::protocol::get_requested_file_from_message(msg);
    REQUIRE(deserialized_request.has_value());
    REQUIRE(requested == deserialized_request.value());
  }
}

TEST_CASE("multi message serialization", "[protocol]") {
  mfsync::file_handler::stored_files files;

  for(size_t i = 0; i < 100; ++i)
  {
    std::string name = "file" + std::to_string(i);
    mfsync::file_information info{ name, std::nullopt, i};
    files.insert(info);
  }

  const auto result = mfsync::protocol::create_messages_from_file_info(files, 2342);

  for(const auto& msg : result)
  {
    const auto availables_from_result = mfsync::protocol::get_available_files_from_message(msg, {});
    REQUIRE(availables_from_result.has_value());

    for(const auto& available : availables_from_result.value())
    {
      REQUIRE(files.contains(available.file_info));
      REQUIRE(available.source_port == 2342);
    }
  }
}
