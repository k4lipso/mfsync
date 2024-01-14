#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

#include "mfsync/file_handler.h"
#include "mfsync/protocol.h"
#include "mfsync/file_receive_handler.h"

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
        some_file_info, boost::asio::ip::make_address("8.23.42.17"), 1337, ""};

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
  const auto empty = mfsync::protocol::get_available_files_from_message("");
  REQUIRE(!empty.has_value());

  const auto msg = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                 + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty2 =
      mfsync::protocol::get_available_files_from_message(msg);
  REQUIRE(!empty2.has_value());

  const auto msg2 = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                  + std::string("{}")
                  + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty3 =
      mfsync::protocol::get_available_files_from_message(msg2);
  REQUIRE(!empty3.has_value());

  const auto msg3 = std::string(mfsync::protocol::MFSYNC_HEADER_BEGIN)
                  + std::string("{ \"foo\": \"bar\", \"baz\": 23 }")
                  + std::string(mfsync::protocol::MFSYNC_HEADER_END);
  const auto empty4 =
      mfsync::protocol::get_available_files_from_message(msg3);
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

TEST_CASE("bug: source addr not copied", "[protocol") {
  mfsync::file_handler::stored_files files;
  mfsync::file_information info{ "test_file", std::nullopt, 9000};
  files.insert(info);

  auto address = boost::asio::ip::address::from_string("12.34.56.78");
  auto port = 2342;
  const auto result = mfsync::protocol::create_messages_from_file_info(files, port);

  boost::asio::ip::udp::endpoint endpoint(address, port);
  for(const auto& msg : result)
  {
    const auto availables_from_result = mfsync::protocol::get_available_files_from_message(msg, endpoint);
    for(const auto& available : availables_from_result.value())
    {
      REQUIRE(available.source_address == endpoint.address());
    }
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
    const auto availables_from_result = mfsync::protocol::get_available_files_from_message(msg);
    REQUIRE(availables_from_result.has_value());

    for(const auto& available : availables_from_result.value())
    {
      REQUIRE(files.contains(available.file_info));
      REQUIRE(available.source_port == 2342);
    }
  }
}

TEST_CASE("request files by directory test", "[file_receive_handler]") {
  class file_receive_handler_test : public mfsync::file_receive_handler
  {
  public:
    using file_receive_handler::file_receive_handler;

    size_t fill_request_queue_test()
    {
      file_receive_handler::fill_request_queue();
      return request_queue_.size();
    }
  };

  auto file_handler = mfsync::file_handler();

  //first create and add some available files:
  mfsync::file_handler::available_files availables{{
    { .file_info = { .file_name = "test1.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "test2.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder1/test2.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder2/test2.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder2/test3.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder2/test4.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder1/subfolder1/test1.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder1/subfolder2/test1.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder1/subfolder2/test2.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" },
    { .file_info = { .file_name = "folder1/subfolder2/test3.txt" }, .source_address = {}, .source_port = 1336, .public_key = "" }
  }};

  file_handler.add_available_files(std::move(availables));

  boost::asio::io_context ctx;
  mfsync::crypto::crypto_handler crypto_handler;
  file_receive_handler_test receive_handler{ctx, file_handler, 1, crypto_handler, nullptr};

  //second create files that should be requested:
  std::vector<std::string> files_to_request{{
      "folder1/subfolder2"
  }};

  receive_handler.set_files(files_to_request);
  const auto request_queue_size = receive_handler.fill_request_queue_test();
  REQUIRE(request_queue_size == 3);
}
