// Ориентир на Go для замеров задач, мониторов и каналов BlackBox.
// Код Go исполняет одна нить (GOMAXPROCS=1), как в BlackBox.
//
//	bench-go 1000          // System_devTasks.LoadBench: n сопрограмм, Gosched и счётчик
//	bench-go pair          // парные замеры Async_devBench
//
// Строки BENCH совпадают по именам с Async_devBench.
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
			counter++ // одна нить: гонки нет
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

type msg struct{ n int }

func report(name string, ops int, d time.Duration) {
	fmt.Printf("BENCH go-%s ops=%d totalNs=%d nsPerOp=%d\n", name, ops, d.Nanoseconds(), d.Nanoseconds()/int64(ops))
}

// pingpong: две сопрограммы обмениваются через небуферизованные каналы; op — один обмен (туда и обратно).
func pingpong(n int) {
	a, b := make(chan *msg), make(chan *msg)
	done := make(chan bool)
	go func() {
		for i := 0; i < n; i++ {
			m := <-a
			b <- m
		}
		done <- true
	}()
	t0 := time.Now()
	m := &msg{}
	for i := 0; i < n; i++ {
		a <- m
		m = <-b
	}
	<-done
	report("pingpong", n, time.Since(t0))
}

// buffer: производитель -> канал на 64 -> потребитель; op — одно сообщение.
func buffer(n int) {
	c := make(chan *msg, 64)
	done := make(chan int)
	go func() {
		s := 0
		for i := 0; i < n; i++ {
			s += (<-c).n
		}
		done <- s
	}()
	t0 := time.Now()
	for i := 0; i < n; i++ {
		c <- &msg{i}
	}
	<-done
	report("buffer64", n, time.Since(t0))
}

type box struct {
	mu sync.Mutex
	n  int
}

//go:noinline
func (b *box) touch() { b.mu.Lock(); b.mu.Unlock() }

// mutex: свободный замок, op — пара Lock/Unlock.
func mutex(n int) {
	b := &box{}
	t0 := time.Now()
	for i := 0; i < n; i++ {
		b.touch()
	}
	report("monitor", n, time.Since(t0))
}

// selectk: k отправителей по своим небуферизованным каналам, получатель выбирает из k; op — один select.
func selectk(k, n int) {
	cs := make([]chan *msg, k)
	for i := range cs {
		cs[i] = make(chan *msg)
	}
	per := n / k
	for i := 0; i < k; i++ {
		go func(c chan *msg) {
			m := &msg{}
			for j := 0; j < per; j++ {
				c <- m
			}
		}(cs[i])
	}
	t0 := time.Now()
	got := 0
	total := per * k
	if k == 2 {
		for got < total {
			select {
			case <-cs[0]:
			case <-cs[1]:
			}
			got++
		}
	} else {
		for got < total {
			select {
			case <-cs[0]:
			case <-cs[1]:
			case <-cs[2]:
			case <-cs[3]:
			case <-cs[4]:
			case <-cs[5]:
			case <-cs[6]:
			case <-cs[7]:
			}
			got++
		}
	}
	report("select"+strconv.Itoa(k), total, time.Since(t0))
}

// spawn: создать и дождаться n коротких сопрограмм подряд; op — одна сопрограмма.
func spawn(n int) {
	var wg sync.WaitGroup
	counter := 0
	t0 := time.Now()
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func() { counter++; wg.Done() }()
	}
	wg.Wait()
	report("spawn", n, time.Since(t0))
}

// yield: две сопрограммы уступают друг другу; op — один Gosched.
func yield(n int) {
	var wg sync.WaitGroup
	wg.Add(2)
	f := func() {
		for i := 0; i < n/2; i++ {
			runtime.Gosched()
		}
		wg.Done()
	}
	t0 := time.Now()
	go f()
	go f()
	wg.Wait()
	report("yield", n, time.Since(t0))
}

// sleepers: n сопрограмм, каждая k раз спит 1..10 мс; op — одно пробуждение.
func sleepers(n, k int) {
	var wg sync.WaitGroup
	wg.Add(n)
	t0 := time.Now()
	for i := 1; i <= n; i++ {
		go func(id int) {
			for j := 1; j <= k; j++ {
				time.Sleep(time.Duration(1+(id+j)%10) * time.Millisecond)
			}
			wg.Done()
		}(i)
	}
	wg.Wait()
	report("sleep10k", n*k, time.Since(t0))
}

func pairs() {
	const m = 1000000
	pingpong(m)
	buffer(m)
	mutex(m)
	selectk(2, m)
	selectk(8, m)
	spawn(100000)
	yield(m)
	sleepers(10000, 20)
}

func main() {
	runtime.GOMAXPROCS(1)
	if len(os.Args) > 1 && os.Args[1] == "pair" {
		pairs()
		return
	}
	n := 1000
	if len(os.Args) > 1 {
		n, _ = strconv.Atoi(os.Args[1])
	}
	bench(n)
}
