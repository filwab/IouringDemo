#include <cstdio>
#include <iostream>
#include <liburing.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>

using namespace std;

#define RING_QD (32)
#define RING_FLAG (IORING_SETUP_IOPOLL)


#define KB (1024LL)
#define MB (1024 * 1024LL)
#define GB (1024 * 1024 * 1024LL)

#define MAX_IOSIZE (10 * MB)

#define USER_SPACE_LEN (10 * GB)


struct io_request {
    int fd;
    uint64_t offset;
    uint64_t len;
    char *buf;
    bool read;
};

template <class Container>
void str_split(const string& str, Container& cont,
              const string& delims = " ")
{
    size_t current, previous = 0;
    current = str.find_first_of(delims);
    while (current != string::npos) {
        cont.push_back(str.substr(previous, current - previous));
        previous = current + 1;
        current = str.find_first_of(delims, previous);
    }
    cont.push_back(str.substr(previous, current - previous));
}


void submit_io(struct io_uring *ring, struct io_request *req) {
    //打印IO请求
    cout <<" offset: " << req->offset << " len: " << req->len << " read: " << req->read << endl;
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_rw(req->read ? IORING_OP_READ : IORING_OP_WRITE, sqe, req->fd, req->buf, req->len, req->offset);
    // printf("sqe->user_data:%llu\n", sqe->user_data);
    io_uring_submit(ring);
}

void wait_completion(struct io_uring *ring) {
    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(ring, &cqe);
    // printf("cqe->user_data:%llu\n", cqe->user_data);
    //打印res
    cout << "res: " << cqe->res << endl;
    io_uring_cqe_seen(ring, cqe);
}

int main() {
    struct io_uring ring;
    io_uring_queue_init(RING_QD, &ring, RING_FLAG);

    // Open your file here
    int fd = open("/dev/nvme0n1", O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Allocate buffer for IO
    char *buffer = new char[MAX_IOSIZE];
    if (!buffer) {
        std::cerr << "Failed to allocate buffer" << std::endl;
        return 1;
    }
    string line;
    ifstream  file("../Trace/mytest.log");
    if (!file.is_open()) {
        cerr << "Failed to open trace file." << endl;
        return 1;
    }
    while (getline(file, line)) {
        if (line.empty())
            continue;
        vector<string> lineSplit;
        str_split(line, lineSplit, "\t");
        uint64_t offset = atoll(lineSplit[1].c_str());
        uint64_t length = atoll(lineSplit[2].c_str());
        if (length > 10 * MB) {
            continue;
        }
        offset = offset % USER_SPACE_LEN;
        offset = offset / (4 * KB) * (4 * KB);

        cout << "offset: " << offset << " length: " << length << endl;

        if (lineSplit[0] == "R") {
            // Read operation
            io_request req = {fd, offset, length, buffer, true};
            submit_io(&ring, &req);
            wait_completion(&ring);
        } else if (lineSplit[0] == "W") {
            // Write operation
            //  Fill buffer with random data
            for (int i = 0; i < length; i++) {
                buffer[i] = rand() % 256;
            }
            io_request req = {fd, offset, length, buffer, false};
            submit_io(&ring, &req);
            wait_completion(&ring);
        }
    }

    // Cleanup
    delete[] buffer;
    close(fd);
    io_uring_queue_exit(&ring);

    return 0;
}
