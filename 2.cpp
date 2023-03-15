#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

static const std::filesystem::path INPUT_FILEPATH{ "img01.bmp" };
static constexpr auto HEADER_SIZE{ 54U };
static constexpr auto THREADS_NUM{ 5U };

uint64_t proceed(const std::vector<char>& data);
void* job(void* arg);

struct task_t
{
    const std::vector<char>& data;
    pthread_mutex_t* mutex;
    uint64_t& cnt;
    size_t tid;

    task_t(const std::vector<char>& data, pthread_mutex_t* mutex, uint64_t& cnt, size_t tid)
        : data(data)
        , mutex(mutex)
        , cnt(cnt)
        , tid(tid)
    {}
};

int main()
{
    std::ifstream input(INPUT_FILEPATH, std::ios::in | std::ios::binary);
    if (!input.is_open())
    {
        std::cerr << "Failed to open file: path=" << INPUT_FILEPATH << '\n';
        return -1;
    }

    std::vector<char> data;
    {
        std::array<char, HEADER_SIZE> header;
        input.read(header.data(), header.size());

        const auto data_offset = *reinterpret_cast<uint32_t*>(&header[10]);
        const auto width = *reinterpret_cast<uint32_t*>(&header[18]);
        const auto height = *reinterpret_cast<uint32_t*>(&header[22]);
        std::cout << "data_offset=" << data_offset << ", width=" << width << ", height=" << height << '\n';

        data.resize(data_offset - HEADER_SIZE);
        input.read(data.data(), data.size());

        data.reserve(((width * 3 + 3) & (~3)) * height);

        std::vector<char> row_padded((width * 3 + 3) & (~3));
        for (size_t i{}; i < height; ++i)
        {
            input.read(row_padded.data(), row_padded.size());
            for (size_t j{}; j < width * 3; j += 3)
            {
                data.push_back(row_padded[j]);
                data.push_back(row_padded[j + 1]);
                data.push_back(row_padded[j + 2]);
            }
        }

        assert(data.size() % 3 == 0);
    }

    const auto res = proceed(data);
    std::cout << "2: cnt=" << res << '\n';

    return 0;
}

uint64_t proceed(const std::vector<char>& data)
{
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);

    uint64_t cnt{};
    std::array<std::shared_ptr<task_t>, THREADS_NUM> tasks;
    std::vector<pthread_t> workers(THREADS_NUM);
    for (size_t i{}; i < THREADS_NUM; ++i)
    {
        tasks[i] = std::make_shared<task_t>(data, &mutex, cnt, i);
        pthread_create(&workers[i], NULL, &job, reinterpret_cast<void*>(tasks[i].get()));
    }

    for (auto&& worker : workers)
        pthread_join(worker, NULL);

    pthread_mutex_destroy(&mutex);

    return cnt;
}

void* job(void* arg)
{
    const auto* task = reinterpret_cast<task_t*>(arg);
    assert(task);

    const long end = task->data.size() - (task->tid + 1) * task->data.size() / THREADS_NUM - 1;
    const long start = task->data.size() - task->tid * task->data.size() / THREADS_NUM - 1;
    assert((start - end) % 3 == 0);

    uint64_t local_cnt{};
    for (long i = start; i > end; i -= 3)
        if (static_cast<size_t>(task->data[i]) * task->data[i - 1] * task->data[i - 2] < 1000)
            ++local_cnt;

    pthread_mutex_lock(task->mutex);
    task->cnt += local_cnt;
    std::cout << syscall(SYS_gettid) << "(" << task->tid << "): local_cnt=" << local_cnt << '\n';
    pthread_mutex_unlock(task->mutex);

    return nullptr;
}