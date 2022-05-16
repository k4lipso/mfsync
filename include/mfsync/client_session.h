#pragma once

#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"
#include "mfsync/deque.h"

namespace mfsync::filetransfer
{

using namespace indicators;

class progress_handler
{
public:
  progress_handler()
  {
    show_console_cursor(false);
    bars_.set_option(option::HideBarWhenComplete{false});

    worker_thread_ = std::thread{[this]()
    {
      while(running_)
      {
        for(int i = 0; i < percentages_.size(); ++i)
        {
          if(percentages_[i] == 100)
          {
            if(!bars_[i].is_completed())
            {
              bars_[i].set_progress(percentages_[i]);
              bars_[i].mark_as_completed();
            }

            continue;
          }

          bars_[i].set_progress(percentages_[i]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }};
  }

  ~progress_handler()
  {
    show_console_cursor(true);
  }

  std::size_t create_bar(const std::string& filename)
  {
    std::unique_lock lk{mutex_};

    auto bar = std::make_shared<ProgressBar>(option::BarWidth{50},
                                             option::ForegroundColor{Color::red},
                                             //option::Start{"\r["},
                                             option::ShowPercentage{true},
                                             option::PrefixText{filename});
    others_.emplace_back(bar);

    auto index = bars_.push_back(*others_.back().get());
    percentages_.push_back(0);
    return index;
  }

  ProgressBar& get(size_t index)
  {
    return bars_[index];
  }

  void set_progress(size_t index, int percentage)
  {
    //bars_[index].set_progress(percentage);
    percentages_[index] = percentage;
  }

  template<typename Option>
  void set_option(size_t index, Option option)
  {
    bars_[index].set_option(option);
  }

private:
  DynamicProgress<ProgressBar> bars_;
  std::vector<int> percentages_;
  std::vector<std::shared_ptr<ProgressBar>> others_;
  std::mutex mutex_;
  std::thread worker_thread_;
  bool running_ = true;
};


class session_base
{
public:
  virtual ~session_base() = default;
};

template<typename SocketType>
class client_session_base : public session_base, public std::enable_shared_from_this<client_session_base<SocketType>>
{
public:
  client_session_base() = delete;
  client_session_base(boost::asio::io_context& context,
                      SocketType socket,
                      mfsync::concurrent::deque<available_file>& deque,
                      mfsync::file_handler& handler);
  virtual ~client_session_base() = default;

  SocketType& get_socket();
  virtual void start_request() = 0;
  void request_file();

  void set_progress(progress_handler* progress)
  {
    progress_ = progress;
  }

protected:
  void read_file_request_response();
  void handle_read_file_request_response(boost::system::error_code const &error, std::size_t bytes_transferred);
  void read_file_chunk();
  void handle_read_file_chunk(boost::system::error_code const &error, std::size_t bytes_transferred);
  void handle_error();

  boost::asio::io_context& io_context_;
  SocketType socket_;
  requested_file requested_;
  size_t bytes_written_to_requested_ = 0;
  mfsync::concurrent::deque<available_file>& deque_;
  mfsync::file_handler& file_handler_;
  std::string message_;
  boost::asio::streambuf stream_buffer_;
  std::vector<uint8_t> readbuf_;
  mfsync::ofstream_wrapper ofstream_;
  progress_handler* progress_;
  std::optional<std::size_t> progress_index_ = std::nullopt;
  int percentage_ = 0;
};

class client_session : public client_session_base<boost::asio::ip::tcp::socket>
{
public:
  client_session() = delete;
  client_session(boost::asio::io_context& context,
                 mfsync::concurrent::deque<available_file>& deque,
                 mfsync::file_handler& handler);
  virtual ~client_session() = default;

  virtual void start_request() override;
};

class client_tls_session : public client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
{
public:
  using base = client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
  client_tls_session() = delete;
  client_tls_session(boost::asio::io_context& context,
                     boost::asio::ssl::context& ssl_context,
                     mfsync::concurrent::deque<available_file>& deque,
                     mfsync::file_handler& handler);
  virtual ~client_tls_session() = default;

  virtual void start_request() override;
  bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx);
  void handshake();
};

} //closing namespace mfsync::filetransfer
