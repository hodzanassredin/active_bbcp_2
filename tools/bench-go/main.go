// Baseline for the Tasks load benchmark (System_devTasks.LoadBench):
// n goroutines, each yields once and increments a counter.
// One OS thread runs Go code (GOMAXPROCS=1), as in BlackBox.
package main

import (
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

func rssKB() int {
	b, err := os.ReadFile("/proc/self/status")
	if err != nil {
		return 0
	}
	for _, l := range strings.Split(string(b), "\n") {
		if strings.HasPrefix(l, "VmRSS:") {
			f := strings.Fields(l)
			v, _ := strconv.Atoi(f[1])
			return v
		}
	}
	return 0
}

func bench(n int) {
	var wg sync.WaitGroup
	counter := 0
	wg.Add(n)
	t0 := time.Now()
	for i := 0; i < n; i++ {
		go func() {
			runtime.Gosched()
			counter++ // one thread: no race
			wg.Done()
		}()
	}
	t1 := time.Now()
	wg.Wait()
	t2 := time.Now()
	fmt.Printf("BENCH go tasks=%d counter=%d setupNs=%d stepNs=%d nsPerTask=%d rssKB=%d\n",
		n, counter, t1.Sub(t0).Nanoseconds(), t2.Sub(t1).Nanoseconds(),
		t2.Sub(t0).Nanoseconds()/int64(n), rssKB())
}

func main() {
	runtime.GOMAXPROCS(1)
	n := 1000
	if len(os.Args) > 1 {
		n, _ = strconv.Atoi(os.Args[1])
	}
	bench(n)
}
