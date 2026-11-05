#pragma once

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <boost/asio/ssl.hpp>

#include <cpr/cpr.h>

#include <memory>
#include <thread>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <sstream>

using client = websocketpp::client<websocketpp::config::asio_tls_client>;
using context_ptr = std::shared_ptr<boost::asio::ssl::context>;

class connection_metadata {
public:
    using ptr = std::shared_ptr<connection_metadata>;

    connection_metadata(int id, websocketpp::connection_hdl hdl, std::string uri);

    void on_open(client * c, websocketpp::connection_hdl hdl);

    void on_fail(client * c, websocketpp::connection_hdl hdl);

    void on_close(client * c, websocketpp::connection_hdl hdl);

    friend std::ostream & operator<< (std::ostream & out, connection_metadata const & data);

private:
    int m_id;
    websocketpp::connection_hdl m_hdl;
    std::string m_status;
    std::string m_uri;
    std::string m_server;
    std::string m_error_reason;
};

class websocket_endpoint {
public:
    websocket_endpoint();

    int connect(std::string const & uri,
                std::function<void(websocketpp::connection_hdl, client::message_ptr)> msg_handler = nullptr);

    context_ptr on_tls_init(websocketpp::connection_hdl);

    connection_metadata::ptr get_metadata(int id) const;

private:
    using con_list = std::map<int, connection_metadata::ptr>;

    client m_endpoint;
    std::shared_ptr<std::thread> m_thread;
    con_list m_connection_list;
    int m_next_id;
};
