#include "net/OrderBookClient.hpp"
#include <iostream>
#include <sstream>

connection_metadata::connection_metadata(int id, websocketpp::connection_hdl hdl, std::string uri)
    : m_id(id), m_hdl(hdl), m_status("Connecting"), m_uri(uri), m_server("N/A") {}

void connection_metadata::on_open(client* c, websocketpp::connection_hdl hdl) {
    m_status = "Open";
    client::connection_ptr con = c->get_con_from_hdl(hdl);
    m_server = con->get_response_header("Server");
}

void connection_metadata::on_fail(client* c, websocketpp::connection_hdl hdl) {
    m_status = "Failed";
    client::connection_ptr con = c->get_con_from_hdl(hdl);
    m_server = con->get_response_header("Server");
    m_error_reason = con->get_ec().message();
}

void connection_metadata::on_close(client* c, websocketpp::connection_hdl hdl) {
    m_status = "Closed";
    client::connection_ptr con = c->get_con_from_hdl(hdl);
    std::stringstream s;
    s << "close code: " << con->get_remote_close_code()
      << " (" << con->get_remote_close_reason() << ")";
    m_error_reason = s.str();
}

std::ostream& operator<<(std::ostream& out, const connection_metadata& data) {
    out << "> URI: " << data.m_uri << "\n"
        << "> Status: " << data.m_status << "\n"
        << "> Remote Server: " << (data.m_server.empty() ? "None Specified" : data.m_server) << "\n"
        << "> Error/close reason: " << (data.m_error_reason.empty() ? "N/A" : data.m_error_reason);
    return out;
}

websocket_endpoint::websocket_endpoint() : m_next_id(0) {
    m_endpoint.clear_access_channels(websocketpp::log::alevel::all);
    m_endpoint.clear_error_channels(websocketpp::log::elevel::all);
    m_endpoint.init_asio();
    m_endpoint.start_perpetual();
    m_endpoint.set_tls_init_handler([this](websocketpp::connection_hdl hdl) { return this->on_tls_init(hdl); });
    m_thread.reset(new std::thread(&client::run, &m_endpoint));
}

context_ptr websocket_endpoint::on_tls_init(websocketpp::connection_hdl /*hdl*/) {
    context_ptr ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
    try {
        ctx->set_options(boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::no_sslv3 |
                         boost::asio::ssl::context::single_dh_use);
        ctx->set_default_verify_paths();
        ctx->set_verify_mode(boost::asio::ssl::verify_peer);
    } catch (std::exception const& e) {
        std::cout << "TLS initialization error: " << e.what() << std::endl;
    }
    return ctx;
}

int websocket_endpoint::connect(std::string const& uri,
                                std::function<void(websocketpp::connection_hdl, client::message_ptr)> msg_handler) {
    websocketpp::lib::error_code ec;
    client::connection_ptr con = m_endpoint.get_connection(uri, ec);
    if (ec) {
        std::cout << "> Connect initialization error: " << ec.message() << std::endl;
        return -1;
    }
    int new_id = m_next_id++;
    connection_metadata::ptr metadata_ptr = std::make_shared<connection_metadata>(new_id, con->get_handle(), uri);
    m_connection_list[new_id] = metadata_ptr;

    con->set_open_handler([metadata_ptr, this](websocketpp::connection_hdl hdl) {
        metadata_ptr->on_open(&m_endpoint, hdl);
    });
    con->set_fail_handler([metadata_ptr, this](websocketpp::connection_hdl hdl) {
        metadata_ptr->on_fail(&m_endpoint, hdl);
    });
    con->set_close_handler([metadata_ptr, this](websocketpp::connection_hdl hdl) {
        metadata_ptr->on_close(&m_endpoint, hdl);
    });
    if (msg_handler) {
        con->set_message_handler(msg_handler);
    }
    m_endpoint.connect(con);
    return new_id;
}

connection_metadata::ptr websocket_endpoint::get_metadata(int id) const {
    auto it = m_connection_list.find(id);
    if (it == m_connection_list.end()) return connection_metadata::ptr();
    return it->second;
}
