
#include "mcp_stdio_transport.hpp"

#include <stdexcept>

toolcall::mcp_stdio_transport::mcp_stdio_transport(std::vector<std::string> argv)
    : argv_(std::move(argv))
{
}

void toolcall::mcp_stdio_transport::start() {
    throw std::logic_error(std::string("Function not implemented: ") + __func__);
}

void toolcall::mcp_stdio_transport::stop() {
    throw std::logic_error(std::string("Function not implemented: ") + __func__);
}

bool toolcall::mcp_stdio_transport::send(const mcp::message_variant & /*request*/) {
    throw std::logic_error(std::string("Function not implemented: ") + __func__);
}
