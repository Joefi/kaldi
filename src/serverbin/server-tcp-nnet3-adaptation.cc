#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>

#include "feat/wave-reader.h"
#include "online2/online-nnet3-decoding.h"
#include "online2/online-nnet2-feature-pipeline.h"
#include "online2/onlinebin-util.h"
#include "online2/online-timing.h"
#include "online2/online-endpoint.h"
#include "fstext/fstext-lib.h"
#include "lat/lattice-functions.h"
#include "util/kaldi-thread.h"
#include "nnet3/nnet-utils.h"

namespace kaldi {
    struct FILEINFO
    {
        int fileLength;
        char fileName[100];
    };

    class TcpServer {
    public:
        explicit TcpServer(int read_timeout);
        ~TcpServer();

        bool Listen(int32 port);  // start listening on a given port
        int32 Accept();  // accept a client and return its descriptor

        bool ReadChunk(size_t len); // get more data and return false if end-of-stream

        size_t GetBuffer(char buffer[], int len);

        bool Write(const std::string& msg); // write to accepted client
        bool WriteLn(const std::string& msg, const std::string& eol = "\n"); // write line to accepted client

        void Disconnect();

    private:
        struct ::sockaddr_in h_addr_;
        int32 server_desc_, client_desc_;
        char* samp_buf_;
        size_t buf_len_, has_read_;
        pollfd client_set_[1];
        int read_timeout_;
    };
}

int main(int argc, char* argv[]) {
    try
    {
        using namespace kaldi;
        const char* usage =
            "Reads in audio zip file from a network socket and performs adaptation\n"
            "with neural nets (nnet3 setup),\n" 
            "\n"
            "Usage: server-tcp-nnet3-adaptation [options] <save-dir> <adaptation-shell-script>\n";

        ParseOptions po(usage);

        int port_num = 5051;
        int read_timeout = 3;

        po.Register("read-timeout", &read_timeout,
            "Number of seconds of timout for TCP audio data to appear on the stream. Use -1 for blocking.");
        po.Register("port-num", &port_num,
            "Port number the server will listen on.");

        po.Read(argc, argv);

        if (po.NumArgs() != 2) {
            po.PrintUsage();
            return 1;
        }

        std::string save_dir = po.GetArg(1),
            shell_script = po.GetArg(2);

        TcpServer server(read_timeout);

        server.Listen(port_num);

        while (true) {

            server.Accept();

            size_t chunk_len = 2048;

            bool eos = false;

            while (!eos)
            {
                FILEINFO fileInfo;
                eos = !server.ReadChunk(sizeof(fileInfo));
                size_t len = server.GetBuffer((char*)&fileInfo, sizeof(fileInfo));
                std::string filename(fileInfo.fileName);
                std::string s_file_path = save_dir + '/' + filename;

                FILE* fp = fopen(s_file_path.c_str(), "w");
                if (NULL == fp)
                {
                    KALDI_VLOG(1) << "File:" << s_file_path << "Can Not Open To Write\n";
                    break;
                }
                char buffer[chunk_len];
                while(true)
                {
                    eos = !server.ReadChunk(chunk_len);

                    if (eos) {
                        fclose(fp);

                        std::string msg = "start adaptation...";
                        server.WriteLn(msg);
                        std::string command = shell_script + " " + s_file_path;
                        int rv = system(command.c_str());
                        if (WIFEXITED(rv))
                        {
                            KALDI_VLOG(1) << "subprocess exited, exit code:" << WEXITSTATUS(rv);
                            if (0 == WEXITSTATUS(rv))
                            {
                                KALDI_VLOG(1) << "command succeed\n";
                            }
                            else
                            {
                                if (127 == WEXITSTATUS(rv))
                                {
                                    KALDI_VLOG(1) << "command not found\n";
                                }
                                else
                                {
                                    KALDI_VLOG(1) << "command failed:" << strerror(WEXITSTATUS(rv));
                                }
                            }
                        }
                        else
                        {
                            KALDI_VLOG(1) << "subprocess exit failed";
                        }

                        //TODO
                        server.Disconnect();
                        break;
                    }
                    size_t len = server.GetBuffer(buffer, chunk_len);
                    if (fwrite(buffer, sizeof(char), len, fp) < len)
                    {
                        KALDI_VLOG(1) << "File:\t" << s_file_path << "Write Failed\n";
                        break;
                    }

                }
        
            }
        

        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what();
        return -1;
    }



}


namespace kaldi {
    TcpServer::TcpServer(int read_timeout) {
        server_desc_ = -1;
        client_desc_ = -1;
        samp_buf_ = NULL;
        buf_len_ = 0;
        read_timeout_ = 1000 * read_timeout;
    }

    bool TcpServer::Listen(int32 port) {
        h_addr_.sin_addr.s_addr = INADDR_ANY;
        h_addr_.sin_port = htons(port);
        h_addr_.sin_family = AF_INET;

        server_desc_ = socket(AF_INET, SOCK_STREAM, 0);

        if (server_desc_ == -1) {
            KALDI_ERR << "Cannot create TCP socket!";
            return false;
        }

        int32 flag = 1;
        int32 len = sizeof(int32);
        if (setsockopt(server_desc_, SOL_SOCKET, SO_REUSEADDR, &flag, len) == -1) {
            KALDI_ERR << "Cannot set socket options!";
            return false;
        }

        if (bind(server_desc_, (struct sockaddr*) & h_addr_, sizeof(h_addr_)) == -1) {
            KALDI_ERR << "Cannot bind to port: " << port << " (is it taken?)";
            return false;
        }

        if (listen(server_desc_, 1) == -1) {
            KALDI_ERR << "Cannot listen on port!";
            return false;
        }

        KALDI_LOG << "TcpServer: Listening on port: " << port;

        return true;

    }

    TcpServer::~TcpServer() {
        Disconnect();
        if (server_desc_ != -1)
            close(server_desc_);
        delete[] samp_buf_;
    }

    int32 TcpServer::Accept() {
        KALDI_LOG << "Waiting for client...";

        socklen_t len;

        len = sizeof(struct sockaddr);
        client_desc_ = accept(server_desc_, (struct sockaddr*) & h_addr_, &len);

        struct sockaddr_storage addr;
        char ipstr[20];

        len = sizeof addr;
        getpeername(client_desc_, (struct sockaddr*) & addr, &len);

        struct sockaddr_in* s = (struct sockaddr_in*) & addr;
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);

        client_set_[0].fd = client_desc_;
        client_set_[0].events = POLLIN;

        KALDI_LOG << "Accepted connection from: " << ipstr;

        return client_desc_;
    }

    bool TcpServer::ReadChunk(size_t len) {
        if (buf_len_ != len) {
            buf_len_ = len;
            delete[] samp_buf_;
            samp_buf_ = new char[len];
        }

        ssize_t ret;
        int poll_ret;
        size_t to_read = len * sizeof(char);
        has_read_ = 0;
        while (to_read > 0) {
            poll_ret = poll(client_set_, 1, read_timeout_);
            if (poll_ret == 0) {
                KALDI_WARN << "Socket timeout! Disconnecting..." << "(has_read_ = " << has_read_ << ")";
                break;
            }
            if (poll_ret < 0) {
                KALDI_WARN << "Socket error! Disconnecting...";
                break;
            }
            ret = read(client_desc_, static_cast<void*>(samp_buf_ + has_read_), to_read);
            if (ret <= 0) {
                KALDI_WARN << "Stream over...";
                break;
            }
            to_read -= ret;
            has_read_ += ret;
        }
        has_read_ /= sizeof(char);

        return has_read_ > 0;
    }

    size_t TcpServer::GetBuffer(char buffer[], int len) {
        int l = has_read_;
        if (has_read_ > len)
        {   
            l = len;
        }
        memcpy(buffer, samp_buf_, l);
        return l;
    }

    bool TcpServer::Write(const std::string& msg) {

        const char* p = msg.c_str();
        size_t to_write = msg.size();
        size_t wrote = 0;
        while (to_write > 0) {
            ssize_t ret = write(client_desc_, static_cast<const void*>(p + wrote), to_write);
            if (ret <= 0)
                return false;

            to_write -= ret;
            wrote += ret;
        }

        return true;
    }

    bool TcpServer::WriteLn(const std::string& msg, const std::string& eol) {
        if (Write(msg))
            return Write(eol);
        else return false;
    }

    void TcpServer::Disconnect() {
        if (client_desc_ != -1) {
            close(client_desc_);
            client_desc_ = -1;
        }
    }
}  // namespace kaldi



