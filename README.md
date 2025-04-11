# 🌿 FERN: 빠르고 효율적인 복제 기반 체크포인팅 시스템

**FERN (Fast, Efficient ReplicatioN)**은 대규모 LLM 학습을 위한 고성능, 고가용성 체크포인팅 시스템으로, HPC 클러스터 환경에서의 내결함성(서버 장애 시에도 학습 서비스 중단 없이  운영)을 보장합니다. 기존의 **HVAC (High Velocity AI Cache)**를 쓰기 가능한 분산 Key-Value 스토어로 확장하여, 학습 애플리케이션의 수정 없이도 빠르고 견고한 체크포인팅과 복구를 지원합니다.

<img src="doc/img/fern_design.png" width="600"/>

---

## ✨ 주요 특징

- **DRAM 우선 쓰기 버퍼링**: 체크포인트는 PFS(Shared storage)에 저장되기 전에 먼저 DRAM 기반 TMPFS`/dev/shm`에 기록되어, 빠른 I/O를 실현하고 GPU Idle time을 최소화합니다. 
- 
- **NVMe 기반 비동기 이중화**: 데이터는 이웃 노드의 NVMe 스토리지로 백그라운드에서 복제되어 장애에 대비한 복원력을 확보합니다.
    
- **응용의 I/O 인터셉션**: **FERN**은 `LD_PRELOAD`를 활용해 파일 I/O 호출을 가로채며, 애플리케이션 수정 없이 적용할 수 있습니다.
    
- **결정론적인(Deterministic) 이중화 데이터 배치 전략**: 해시 기반으로 Index 및 복제 노드를 선택해 부하를 분산하고 고가용성을 확보합니다.
    
- **선택적 PFS 플러시**: 체크포인트는 병렬 파일 시스템(PFS)으로 백그라운드에서 비동기적으로 플러시되어 장기 저장을 지원할 수 있습니다.
    

---

## 🏗️ 시스템 아키텍처

FERN은 두 개의 주요 컴포넌트로 구성됩니다:

- **FERN 클라이언트**: 파일 I/O를 가로채 체크포인트 쓰기를 리다이렉트합니다.
    
- **FERN 서버**: 각 노드에서 실행되며 다음 구성 요소로 이루어져 있습니다:
    
    - `Checkpoint Manager`: DRAM에 데이터를 버퍼링합니다.
        
    - `Replication Manager`: NVMe로 복제를 수행합니다.
        
    - `Index Manager`: 체크포인트 메타데이터를 관리합니다.
        

## 📊 성능 하이라이트

- **최대 62% I/O 시간 단축** (기존 병렬 파일 시스템(PFS) 대비) 
    
- **모델 크기 및 노드 수 증가에 따른 선형 확장성**
    
- **빠른 복제를 통한 체크포인트 복구 불가 구간 감소**
    
- **애플리케이션 수정 불필요**<sub>그림: FERN 시스템 아키텍처</sub>
    

<img src="doc/img/fern_io_comparison.png" width="600"/>






# HVAC 오픈 소스 버그 수정
이 프로젝트는 Research-level 오픈소스([HVAC](https://code.ornl.gov/42z/hvac-high-velocity-ai-cache))를 확장해 개발한 프로젝입니다. 
자체 환경에 해당 오픈소스를 포팅하고 멀티 GPU 환경에서 실행시 콜백 처리 과정에서 발생하는 에러를 발견하고 개선해 컨트리뷰션했습니다. 

<details>
<summary> 콜백 동기화 방식 개선 </summary>
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

</details>	

