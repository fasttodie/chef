#include "async_log.h"
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdarg.h>
#include <string>
#include <boost/date_time.hpp>

static pid_t get_tid()
{
    static __thread pid_t tid = 0;
    if (tid == 0) {
        tid = syscall(__NR_gettid);
    }

    return tid;
}

namespace chef
{

async_log::level async_log::level_ = async_log::debug;

async_log::async_log() 
    : end_screen_color_("\033[0m")
    , front_buf_(NULL)
    , back_buf_(NULL)
    , async_mode_(false)
    , screen_(false)
    , run_(false)
{
    level_name_[async_log::debug] = "DEBUG";
    level_name_[async_log::info]  = "INFO";
    level_name_[async_log::warn]  = "WARN";
    level_name_[async_log::error] = "ERROR";
    level_name_[async_log::fatal] = "FATAL";

    screen_color_[async_log::debug] = "\033[01;37m";
    screen_color_[async_log::info]  = "\033[22;36m";
    screen_color_[async_log::warn]  = "\033[01;33m";
    screen_color_[async_log::error] = "\033[22;31m";
    screen_color_[async_log::fatal] = "\033[01;31m";
}

async_log::~async_log()
{
    run_ = false;
    if (async_mode_) {
        log_thread_->join();
    }
}

int async_log::init(level l, bool async_mode)
{
    { /// lock
        boost::lock_guard<boost::mutex> guard(mutex_);
        if (run_) {
            return -1;
        }
    
        level_ = l;
        async_mode_ = async_mode;
        screen_ = !async_mode_;
    
        /// for example
        /// 20140825T193819.chef-VirtualBox.21810.log.chef
        size_t pc_name_len = 128;
        char pc_name[128] = {0};
        gethostname(pc_name, pc_name_len);
        boost::posix_time::ptime time_str = boost::posix_time::second_clock::local_time();
        std::stringstream stream;
        stream << boost::posix_time::to_iso_string(time_str) << "." 
                << pc_name << "." << (uint64_t)getpid() << ".log.chef";
        stream >> file_name_;

        if (async_mode) {
            front_buf_.reset(new buffer());
            back_buf_.reset(new buffer());
            log_thread_.reset(new boost::thread(boost::bind(&async_log::log_thd_fun,
                    this)));
            log_thread_run_up_.wait();
        } else {
            run_= true;
        }
    } /// unlock

    return 0;
}

int async_log::trace(async_log::level l, const char *src_file_name, int line,
        const char *func_name, const char *format,...)
{
    if (!run_) {
        return -1;
    }

    int len = -1;
    /// serialization lock-free,cost more memory
    chef::buffer single_buf(1152);

    // *now time
    boost::posix_time::ptime pt = boost::posix_time::microsec_clock::local_time();
    std::string time_str = boost::posix_time::to_iso_extended_string(pt);
    single_buf.append(time_str.c_str(), time_str.length());

    // *tid
    single_buf.reserve(9);
    snprintf(single_buf.write_pos(), 9, " %6d ", (int)get_tid());
    single_buf.seek_write(8);

    // *level name
    single_buf.reserve(7);
    snprintf(single_buf.write_pos(), 7, "%5s ", level_name_[l].c_str());
    single_buf.seek_write(6);

    // *main
    va_list ap;
    va_start(ap, format);
    single_buf.reserve(1024);
    len = vsnprintf(single_buf.write_pos(), 1024, format, ap);
    if (len > 1024) {
        /// lost..
        len = 1024;
    }
    va_end(ap);
    single_buf.seek_write(len);

    // *src file & line
    //  erase path
    //if (l == async_log::debug) {
        if (!src_file_name) {
            return -1;
        }
        std::string without_path_name(src_file_name);
        std::size_t split_pos = without_path_name.rfind('/');
        if (split_pos != std::string::npos) {
            without_path_name = without_path_name.substr(split_pos + 1, 
                    without_path_name.size() - 1);
        }
        single_buf.reserve(64);
        len = snprintf(single_buf.write_pos(), 64, " - %s:%d", 
                without_path_name.c_str(), line);
        if (len > 64) {
            /// what a long name..
            len = 64;
        }
        single_buf.seek_write(len);
    //}

    // *line end
    single_buf.append("\n", 1);

    { /// lock
        boost::lock_guard<boost::mutex> guard(mutex_);
        if (async_mode_) {
                front_buf_->append(single_buf.read_pos(), single_buf.readable());
        } else {    
            /// sync mode fopen every time
            FILE *fp = fopen(file_name_.c_str(), "ab+");
            fwrite((const void *)single_buf.read_pos(), single_buf.readable(),
                    1, fp);
            fclose(fp);
            if (screen_) { /// kept this 'if', though screen_=!async_mode_
                /// may not complete,cut by '\0'
                single_buf.append("\0", 1);
                printf("%s%s%s", screen_color_[l].c_str(), single_buf.read_pos(),
                        end_screen_color_.c_str());
            }
        }
    } /// unlock

    return 0;
}

void async_log::log_thd_fun()
{
    FILE *fp = fopen(file_name_.c_str(), "ab+");
    if (!fp) {
        run_= false;
        log_thread_run_up_.notify();
        return;
    }           
    fprintf(fp, "async log tid=%d.\n", get_tid()); 
    fflush(fp);
    run_ = true;
    log_thread_run_up_.notify();

    while(run_) {
        bool write = false;
        { /// lock
        boost::lock_guard<boost::mutex> guard(mutex_);
        if (front_buf_->readable() > 0) {
            back_buf_->append(front_buf_->read_pos(), front_buf_->readable());
            front_buf_->reset();
            write = true;
        }
        } /// unlock

        if (write) {
            fwrite((const void *)back_buf_->read_pos(), back_buf_->readable(), 
                    1, fp);
            fflush(fp);
            back_buf_->reset();
        }
        sleep(1);
    }
}

}

