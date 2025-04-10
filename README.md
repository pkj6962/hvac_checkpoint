# 🌿 FERN: Fast, Efficient Replication-based Checkpointing System

**FERN (Fast, Efficient ReplicatioN)** is a high-performance, fault-tolerant checkpointing system designed for large-scale LLM training on HPC clusters. It extends **HVAC (High Velocity AI Cache)** into a write-capable, distributed key-value store to enable fast and resilient checkpointing without requiring changes to the training application.


<img src="doc/img/fern_design.png" width="600"/>





---

## ✨ Key Features

- **DRAM-first Write Buffering**: Checkpoints are first written to `/dev/shm`, leveraging fast CPU memory for ultra-low-latency I/O.
    
- **Asynchronous NVMe Replication**: Data is replicated in the background to a neighboring node’s NVMe storage for fault resilience.
    
- **Transparent I/O Interception**: Uses `LD_PRELOAD` to interpose on file I/O calls without modifying application code.
    
- **Deterministic Placement Strategy**: Index and replica nodes are selected via hashing for load balancing and high availability.
    
- **Optional PFS Flush**: Checkpoints can be flushed asynchronously to the parallel file system for long-term persistence.
    

---


## 🏗️ System Architecture

FERN consists of two main components:

- **FERN Client**: Intercepts file I/O and redirects checkpoint writes.
    
- **FERN Server**: Runs on each node and consists of:
    
    - `Checkpoint Manager`: Buffers data in DRAM.
        
    - `Replication Manager`: Handles NVMe replication.
        
    - `Index Manager`: Maintains checkpoint metadata.
        



## 📊 Performance Highlights

- **Up to 62% reduction in I/O time** compared to PFS
    
- **Linear scalability** with model size and number of nodes
    
- **Shortened checkpoint vulnerability window** via fast replication
    
- **No application modifications required**<sub>Figure: FERN system architecture</sub>



<img src="doc/img/fern_io_comparison.png" width="600"/>






**HVAC 오픈 소스 버그 수정** 
: 비동기 처리 및 콜백 동기화 방식 개선 

HVAC은 내부적으로 Mercury RPC를 사용하며, 콜백 기반의 비동기 통신을 수행합니다. 클라이언트는 서볼 요청을 보낸 후, 콜백이 호출될 때까지 블록되어 데이터 무결성을 보장합니다. 

### 🧩 기존 방식: 전역 Mutex 사용

초기 구현에서는 아래와 같이 전역 `mutex`와 `cond`를 사용하여 RPC 요청의 완료 여부를 동기화됐었습니다.


```C
ssize_t hvac_remote_pread(int fd, void *buf, size_t count, off_t offset)
{
	hvac_client_comm_gen_read_rpc(host, fd, buf, count, offset, hvac_rpc_state_p);
	// Client blocks here until request completion callback routine is called 
	bytes_read = hvac_read_block(host, &done, &bytes_read, &cond, &mutex);   
}


ssize_t hvac_read_block()
{
    ssize_t bytes_read;
    /* wait for callbacks to finish */
    pthread_mutex_lock(&done_mutex);
    while (done != HG_TRUE)
        pthread_cond_wait(&done_cond, &done_mutex);
    bytes_read = read_ret;
    pthread_mutex_unlock(&done_mutex);
    return bytes_read;
}
```

#### ⚠️ 문제점

- 하나의 서버에 여러 클라이언트가 동시에 요청을 보내는 경우, 전역 `mutex` 및 `cond`에 대한 **경쟁 조건(Race Condition)** 발생
    
- 콜백 중첩 호출로 인해 **잘못된 동기화 또는 deadlock** 발생 가능성
    

---

### ✅ 개선 방식: 요청 단위의 세분화된 Lock 적용

각 RPC 요청마다 **별도의 mutex, cond, 상태 변수**를 할당하여, 경합을 없애고 안전하게 동기화를 처리하도록 구조를 개선했습니다.

```C

ssize_t hvac_remote_read(int fd, void *buf, size_t count)
{
		// RPC 요청 전송 시 생성되는 RPC 요청 State 데이터 
		hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
		// 전역으로 선언되어 있던 Mutex를 RPC 요청 당 Mutex로 Fine-grained Lock화 함. 
		// rpc_state 구조체 자료구조 수정 
        hvac_rpc_state_p->bytes_read = &bytes_read;
        hvac_rpc_state_p->done = &done;
        hvac_rpc_state_p->cond = &cond;
        hvac_rpc_state_p->mutex = &mutex;

		hvac_client_comm_gen_read_rpc(host, fd, buf, count, -1, hvac_rpc_state_p); 
}


static hg_return_t
hvac_read_cb(const struct hg_cb_info *info)
{
	// ... 서버의 요청처리 완료에 대한 클라이언트 콜백 처리 
	pthread_mutex_lock(hvac_rpc_state_p->mutex);
    *(hvac_rpc_state_p->done) = HG_TRUE;
    pthread_cond_signal(hvac_rpc_state_p->cond);
    pthread_mutex_unlock(hvac_rpc_state_p->mutex);	
}
```



```C

ssize_t hvac_read_block(uint32_t host, hg_bool_t *done, ssize_t *bytes_read, pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	pthread_mutex_lock(mutex);
	pthread_cond_wait(cond, mutex);
	
    ssize_t result = *bytes_read;
    pthread_mutex_unlock(mutex);

	if (result < 0) 
        return result;
    
    return result
}

```


