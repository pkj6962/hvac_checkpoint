#include <stdio.h>
#include <chrono>
#include <numeric>
#include <thread>

using namespace std;
int main(void)
{

    auto start = chrono::high_resolution_clock::now();
    auto start_duration = start.time_since_epoch(); 
    long long serializedStart = std::chrono::duration_cast<std::chrono::nanoseconds>(start_duration).count(); 
    int64_t s = serializedStart; 

    this_thread::sleep_for(chrono::milliseconds(1500)); // Simulate work
    auto end = chrono::high_resolution_clock::now();

    auto deserializedStart = std::chrono::high_resolution_clock::time_point(std::chrono::nanoseconds(s));    
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - deserializedStart);
    auto duration = latency.count();

    printf("latency: %lld", duration);
}