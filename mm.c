/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team_7",
    /* First member's full name */
    "Jiho Kim",
    /* First member's email address */
    "jiho001108@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};
// 전방 선언
int mm_init(void);
void *mm_malloc(size_t size);
static void place(void *bp, size_t asize);
static void *extend_heap(size_t words);
void mm_free(void *bp);
static void *coalesce(void *bp);
void *mm_realloc(void *ptr, size_t size);
static void *find_fit(size_t asize);

#define WSIZE 4             // word size: 4byte
#define DSIZE 8             // double word size: 8byte
#define CHUNKSIZE (1 << 12) // 힙 확장 시 크기(초기 빈 블록의 크기)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// size와 할당 비트를 결합, header와 footer에 저장할 값
#define PACK(size, alloc) ((size) | (alloc))

// p가 참조하는 워드 반환 (포인터라서 직접 역참조 불가능 -> 타입 캐스팅)
#define GET(p) (*(unsigned int *)(p))

// p에 val 저장
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// 사이즈(~0x7: ...11111000, '&' 연산으로 뒤에 세자리 없어짐)
#define GET_SIZE(p) (GET(p) & ~0x7)

// 할당 상태
#define GET_ALLOC(p) (GET(p) & 0x1)

// Header 포인터
#define HDRP(bp) ((char *)(bp)-WSIZE)

// Footer 포인터 (🚨Header의 정보를 참조해서 가져오기 때문에, Header의 정보를 변경했다면 변경된 위치의 Footer가 반환됨에 유의)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// 다음 블록의 포인터
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))

// 이전 블록의 포인터
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

int mm_init(void)
{
    // 초기 힙 생성
    char *heap_listp;
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) // 4워드 크기의 힙 생성, heap_listp에 힙의 시작 주소값 할당
        return -1;

    PUT(heap_listp, 0);                            // 정렬 패딩
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 Header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 Footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // 에필로그 Header: 프로그램이 할당한 마지막 블록의 뒤에 위치하며, 블록이 할당되지 않은 상태를 나타냄

    // 힙을 CHUNKSIZE bytes로 확장
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

void *mm_malloc(size_t size)
{
    size_t asize;      // 조정된 블록 사이즈
    size_t extendsize; // 확장할 사이즈
    char *bp;

    // 잘못된 요청 분기
    if (size == 0)
        return NULL;

    /* 사이즈 조정 */
    if (size <= DSIZE)     // 8바이트 이하이면
        asize = 2 * DSIZE; // 최소 블록 크기 16바이트 할당 (헤더 4 + 푸터 4 + 저장공간 8)
    else
        asize = DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE); // 8배수로 올림 처리

    /* 가용 블록 검색 */
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize); // 할당
        return bp;        // 새로 할당된 블록의 포인터 리턴
    }

    /* 적합한 블록이 없을 경우 힙확장 */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

static void *find_fit(size_t asize)
{
    void *bp = mem_heap_lo() + 2 * WSIZE; // 첫번째 블록(주소: 힙의 첫 부분 + 8bytes)부터 탐색 시작
    while (GET_SIZE(HDRP(bp)) > 0)
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) // 가용 상태이고, 사이즈가 적합하면
            return bp;                                             // 해당 블록 포인터 리턴
        bp = NEXT_BLKP(bp);                                        // 조건에 맞지 않으면 다음 블록으로 이동해서 탐색을 이어감
    }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp)); // 현재 블록의 크기

    if ((csize - asize) >= (2 * DSIZE)) // 차이가 최소 블록 크기 16보다 같거나 크면 분할
    {
        PUT(HDRP(bp), PACK(asize, 1)); // 현재 블록에는 필요한 만큼만 할당
        PUT(FTRP(bp), PACK(asize, 1));

        PUT(HDRP(NEXT_BLKP(bp)), PACK((csize - asize), 0)); // 남은 크기를 다음 블록에 할당(가용 블록)
        PUT(FTRP(NEXT_BLKP(bp)), PACK((csize - asize), 0));
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1)); // 해당 블록 전부 사용
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static void *extend_heap(size_t words)
{
    char *bp;

    // 더블 워드 정렬 유지
    // size: 확장할 크기
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; // 2워드의 가장 가까운 배수로 반올림 (홀수면 1 더해서 곱함)

    if ((long)(bp = mem_sbrk(size)) == -1) // 힙 확장
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));         // 새 빈 블록 헤더 초기화
    PUT(FTRP(bp), PACK(size, 0));         // 새 빈 블록 푸터 초기화
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 에필로그 블록 헤더 초기화

    return coalesce(bp); // 병합 후 coalesce 함수에서 리턴된 블록 포인터 반환
}

void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * 병합 하기
 * 할당 여부 확인 : 인접 블록의 할당 상태를 확인한다. (GET_ALLOC 매크로 호출)
 * 병합 : 인접 블록 중 가용 상태인 블록이 있다면 현재 블록과 합쳐서 더 큰 하나의 가용 블록으로 만든다.
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록 할당 상태
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록 할당 상태
    size_t size = GET_SIZE(HDRP(bp));                   // 현재 블록 사이즈

    if (prev_alloc && next_alloc) // 모두 할당된 경우
        return bp;

    else if (prev_alloc && !next_alloc) // 다음 블록만 빈 경우
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0)); // 현재 블록 헤더 재설정
        PUT(FTRP(bp), PACK(size, 0)); // 다음 블록 푸터 재설정 (위에서 헤더를 재설정했으므로, FTRP(bp)는 합쳐질 다음 블록의 푸터가 됨)
    }
    else if (!prev_alloc && next_alloc) // 이전 블록만 빈 경우
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 이전 블록 헤더 재설정
        PUT(FTRP(bp), PACK(size, 0));            // 현재 블록 푸터 재설정
        bp = PREV_BLKP(bp);                      // 이전 블록의 시작점으로 포인터 변경
    }
    else // 이전 블록과 다음 블록 모두 빈 경우
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 이전 블록 헤더 재설정
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 다음 블록 푸터 재설정
        bp = PREV_BLKP(bp);                      // 이전 블록의 시작점으로 포인터 변경
    }

    return bp; // 병합된 블록의 포인터 반환
}

// 기존에 할당된 메모리 블록의 크기 변경
// `기존 메모리 블록의 포인터`, `새로운 크기`
void *mm_realloc(void *ptr, size_t size)
{
    /* 예외 처리 */
    if (ptr == NULL) // 포인터가 NULL인 경우 할당만 수행
        return mm_malloc(size);

    if (size <= 0) // size가 0인 경우 메모리 반환만 수행
    {
        mm_free(ptr);
        return 0;
    }

    /* 새 블록에 할당 */
    void *newptr = mm_malloc(size); // 새로 할당한 블록의 포인터
    if (newptr == NULL)
        return NULL; // 할당 실패

    /* 데이터 복사 */
    size_t copySize = GET_SIZE(HDRP(ptr)) - DSIZE; // payload만큼 복사
    if (size < copySize)                           // 기존 사이즈가 새 크기보다 더 크면
        copySize = size;                           // size로 크기 변경 (기존 메모리 블록보다 작은 크기에 할당하면, 일부 데이터만 복사)

    memcpy(newptr, ptr, copySize); // 새 블록으로 데이터 복사

    /* 기존 블록 반환 */
    mm_free(ptr);

    return newptr;
}

// int main(void)
// {
//     int a = 1;
//     int b = 2;

//     return 0;
// }