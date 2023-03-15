#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <Windows.h>

static const std::filesystem::path INPUT_FILEPATH{ "img02.bmp" };
static constexpr auto HEADER_SIZE{ 54U };
static constexpr auto THREADS_NUM{ 5U };

uint64_t proceed(const std::vector<char>& data);
DWORD WINAPI job(LPVOID lpParam);

struct task_t
{
    const std::vector<char>& data;
    HANDLE hEvent;
    uint64_t& cnt;
    size_t tid;

    task_t(const std::vector<char>& data, HANDLE hEvent, uint64_t& cnt, size_t tid)
        : data(data)
        , hEvent(hEvent)
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
    std::cout << "1e: cnt=" << res << '\n';

    return 0;
}

uint64_t proceed(const std::vector<char>& data)
{
    HANDLE hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
    assert(hEvent);

    uint64_t cnt{};
    std::array<std::shared_ptr<task_t>, THREADS_NUM> tasks;
    HANDLE hWorkers[THREADS_NUM];
    for (size_t i{}; i < THREADS_NUM; ++i)
    {
        tasks[i] = std::make_shared<task_t>(data, hEvent, cnt, i);
        hWorkers[i] = CreateThread(NULL, 0LLU, job, reinterpret_cast<LPVOID>(tasks[i].get()), 0LU, NULL);
    }

    WaitForMultipleObjects(THREADS_NUM, hWorkers, TRUE, INFINITE);
    for (auto&& worker : hWorkers)
        CloseHandle(worker);

    CloseHandle(hEvent);

    return cnt;
}

DWORD WINAPI job(LPVOID lpParam)
{
    const auto* task = reinterpret_cast<task_t*>(lpParam);
    assert(task);

    const long end = task->data.size() - (task->tid + 1) * task->data.size() / THREADS_NUM - 1;
    const long start = task->data.size() - task->tid * task->data.size() / THREADS_NUM - 1;
    assert((start - end) % 3 == 0);

    uint64_t local_cnt{};
    for (long i = start; i > end; i -= 3)
        if (static_cast<size_t>(task->data[i]) * task->data[i - 1] * task->data[i - 2] < 1000)
            ++local_cnt;

    std::cout << GetCurrentThreadId() << "(" << task->tid << "): local_cnt=" << local_cnt << '\n';

    WaitForSingleObject(task->hEvent, INFINITE);
    task->cnt += local_cnt;
    SetEvent(task->hEvent);

    return TRUE;
}