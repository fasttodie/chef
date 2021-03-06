#include "io_tcp.h"
#include "chef_async_log.h"
#include "chunk.h"
#include <deque>
#include <boost/unordered_map.hpp>
#include <boost/thread.hpp>
using namespace chef;
class protocol;

io_tcp_ptr srv;
uint64_t id = 0;
boost::unordered_map<uint64_t, protocol *> protocols;
boost::shared_mutex protocols_mutex;
std::deque<boost::shared_ptr<buffer> > history_msgs;
const int history_msg_max_num = 100;

class protocol
{
public:
    protocol(cio_conn_t &cc) :
        inbuf_(1024),
        cc_(cc),
        chunk_len_(0)
    {
        boost::lock_guard<boost::shared_mutex> guard(protocols_mutex);
        id_ = ++id;
        protocols[id_] = this;
        std::deque<boost::shared_ptr<buffer> >::iterator iter = 
                history_msgs.begin();
        for (; iter != history_msgs.end(); ++iter) {
            srv->write(cc, (*iter)->read_pos(), (*iter)->readable());
        }
    }
    ~protocol() 
    {
        boost::lock_guard<boost::shared_mutex> guard(protocols_mutex);
        protocols.erase(id_);
    }

    void on_read(void *buf, uint32_t len)
    {
        inbuf_.append((const char *)buf, len);
        for (; ; ) {
            if (chunk_len_ == 0) {
                if (inbuf_.readable() < sizeof(uint32_t)) {
                    return;
                }
                chunk_len_ = parse_uint32(inbuf_.read_pos());
                if (chunk_len_ == 0 || chunk_len_ > 65535) {
                    CHEF_TRACE_DEBUG("parse error,%u.", chunk_len_);
                    srv->close(cc_);
                    return;
                }
            }
            if (inbuf_.readable() < chunk_len_) {
                return;
            }
            {/// lock
            boost::shared_lock<boost::shared_mutex> guard(protocols_mutex);
            auto iter = protocols.begin();
            for (; iter != protocols.end(); ++iter) {
                srv->write(iter->second->cc_, inbuf_.read_pos(), chunk_len_);
            }
            }/// unlock
            if (history_msgs.size() >= history_msg_max_num) {
                history_msgs.pop_front();
            }
            boost::shared_ptr<buffer> msg(new buffer(chunk_len_));
            msg->append(inbuf_.read_pos(), chunk_len_);
            history_msgs.push_back(msg);
            inbuf_.erase(chunk_len_);
            chunk_len_ = 0;
        }
    }

private:
    buffer inbuf_;
    cio_conn_t cc_;
    uint32_t chunk_len_;
    uint64_t id_;
};

void accept_cb(cio_conn_t cc, const char *peer_ip, uint16_t peer_port)
{
    protocol *p = new protocol(cc);
    cc.arg_ = p;
    srv->set_user_arg(cc);
    srv->enable_read(cc);
}
void read_cb(cio_conn_t cc, void *buf, uint32_t len)
{
    protocol *p = (protocol *)cc.arg_;
    p->on_read(buf, len);
}
void close_cb(cio_conn_t cc)
{
    protocol *p = (protocol *)cc.arg_;
    delete p;
}
void error_cb(cio_conn_t cc, uint8_t error_no)
{
    srv->close(cc);
}

void run(io_tcp_ptr srv)
{
    srv->run();
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: <ip> <port>\n");
        return 0;
    }
    chef::async_log::get_mutable_instance().init();
    srv = io_tcp::create(argv[1], atoi(argv[2]), 4, accept_cb, NULL, read_cb,
            close_cb, error_cb, NULL);
    if (!srv) {
        CHEF_TRACE_DEBUG("user:init fail.");
        return 0;
    }
    boost::thread t(boost::bind(run, srv));
    getchar();
    printf("bye bye.\n");
    return 0;
}

