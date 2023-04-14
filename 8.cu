#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <cuda_runtime.h>
#include <cooperative_groups.h>
#include <device_launch_parameters.h>

static const std::filesystem::path INPUT_FILEPATH{ "img03.bmp" };
static constexpr auto HEADER_SIZE{ 54U };
static constexpr auto THREADS_PER_BLOCK{ 512U };

uint64_t proceed(const std::vector<char>& data);

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

	std::cout << "8b: cnt=" << proceed(data) << '\n';

	cudaDeviceReset();

	return 0;
}

template <size_t BlockSize>
__global__ void cuda_proceed(char* data, const size_t data_size, uint64_t* cnt)
{
	const auto tid = threadIdx.x;
	const auto supply = (data_size % BlockSize) / 3;
	const auto spl_tmp = BlockSize - 1 - tid;
	const auto cull_data_size = data_size - supply * 3;
	const long end = data_size - 1 - (tid + 1) * cull_data_size / BlockSize - 3 * (spl_tmp < supply ? (supply - spl_tmp) : 0);
	const long start = data_size - 1 - tid * cull_data_size / BlockSize - 3 * (spl_tmp < supply ? (supply - spl_tmp - 1) : 0);
	uint64_t local_cnt{};
	for (long i = start; i > end; i -= 3)
		if (static_cast<size_t>(data[i]) * data[i - 1] * data[i - 2] < 1000)
			++local_cnt;

	__shared__ uint64_t shared_cnt[BlockSize];
	shared_cnt[tid] = local_cnt;
	__syncthreads();

	for (int reduction_size = BlockSize / 2; reduction_size; reduction_size >>= 1)
	{
		if (tid < reduction_size)
			shared_cnt[tid] += shared_cnt[tid + reduction_size];
		__syncthreads();
	}

	if (tid == 0)
		*cnt = shared_cnt[0];
}

uint64_t proceed(const std::vector<char>& data)
{
	char* dev_data;
	cudaMalloc(&dev_data, data.size() * sizeof(char));
	cudaMemcpy(dev_data, data.data(), data.size() * sizeof(char), cudaMemcpyHostToDevice);

	uint64_t* dev_cnt;
	cudaMalloc(&dev_cnt, sizeof(uint64_t));
	cudaMemcpy(dev_data, data.data(), data.size() * sizeof(char), cudaMemcpyHostToDevice);

	const dim3 block_size(THREADS_PER_BLOCK, 1, 1);
	const dim3 grid_size(1, 1, 1);
	cuda_proceed<THREADS_PER_BLOCK> << <grid_size, block_size >> > (dev_data, data.size(), dev_cnt);
	cudaDeviceSynchronize();

	size_t cnt{};
	cudaMemcpy(&cnt, dev_cnt, sizeof(uint64_t), cudaMemcpyDeviceToHost);

	cudaFree(dev_cnt);
	cudaFree(dev_data);

	return cnt;
}