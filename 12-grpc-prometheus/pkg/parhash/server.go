package parhash

import (
	"context"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"
	"fs101ex/pkg/workgroup"
    "log"
    "net"
    "sync"
	"time"
    hashpb "fs101ex/pkg/gen/hashsvc"
    parhashpb "fs101ex/pkg/gen/parhashsvc"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int

	Prom prometheus.Registerer
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
//
// The server must report the following performance counters to Prometheus:
//
//  1. nr_nr_requests: a counter that is incremented every time a call
//     is made to ParallelHash(),
//
//  2. subquery_durations: a histogram that tracks durations of calls
//     to backends.
//     It must have a label `backend`.
//     Each subquery_durations{backed=backend_addr} must be a histogram
//     with 24 exponentially growing buckets ranging from 0.1ms to 10s.
//
// Both performance counters must be placed to Prometheus namespace "parhash".
type Server struct {
    conf Config

    sem *semaphore.Weighted

    mutex sync.Mutex
    globalCounter int

    stop context.CancelFunc
    l    net.Listener
    wg   sync.WaitGroup

	nr_nr_requests     prometheus.Counter
	subquery_durations *prometheus.HistogramVec
}

func New(conf Config) *Server {
    return &Server{
        conf: conf,
        sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
        globalCounter: 0,
    }
}

func (s *Server) Start(ctx context.Context) (err error) {
    defer func() { err = errors.Wrap(err, "Start()") }()

    /* implement me */
    ctx, s.stop = context.WithCancel(ctx)

    s.l, err = net.Listen("tcp", s.conf.ListenAddr)
    if err != nil {
        return err
    }

    srv := grpc.NewServer()
    parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.nr_nr_requests = prometheus.NewCounter(prometheus.CounterOpts{
		Namespace: "parhash",
		Name:      "nr_requests",
		Help: "Number of requests",
	})
	s.subquery_durations = prometheus.NewHistogramVec(prometheus.HistogramOpts{
		Namespace: "parhash",
		Name:      "subquery_durations",
		Help: "Subquery durations",
		Buckets:   prometheus.ExponentialBucketsRange(0.1, 10000, 24),
	},
		[]string{"backend"},
	)
	s.conf.Prom.MustRegister(s.nr_nr_requests)
	s.conf.Prom.MustRegister(s.subquery_durations)

    s.wg.Add(2)
    go func() {
        defer s.wg.Done()

        srv.Serve(s.l)
    }()
    go func() {
        defer s.wg.Done()

        <-ctx.Done()
        s.l.Close()
    }()

    return nil
}

func (s *Server) ListenAddr() string {
    /* implement me */
    return s.l.Addr().String()
}

func (s *Server) Stop() {
    /* implement me */
    s.stop()
    s.wg.Wait()
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {

	s.nr_nr_requests.Inc()
    client := make([]hashpb.HashSvcClient, len(s.conf.BackendAddrs))
    wg := workgroup.New(workgroup.Config{Sem: s.sem})
    hashes := make([][]byte, len(req.Data))

    for i, back_addr := range s.conf.BackendAddrs {
        connection, err := grpc.Dial(back_addr, grpc.WithInsecure())
        if err != nil {
            log.Fatalf("Couldn't connect to backend addr %s with error %s", s.conf.BackendAddrs[i], err)
        }
        defer connection.Close()
        client[i] = hashpb.NewHashSvcClient(connection)
    }

    for i, buf := range req.Data {
        buf_idx := i
        curr_buf := buf
        wg.Go(ctx, func(ctx context.Context) error {
            s.mutex.Lock()
            idx := s.globalCounter % len(client)
            s.globalCounter++
            hashReq := hashpb.HashReq{Data: curr_buf}
            s.mutex.Unlock()
			start := time.Now()
            hash, err := client[idx].Hash(ctx, &hashReq)
			end := time.Since(start)
            if err != nil {
                return err
            }
			s.subquery_durations.With(prometheus.Labels{"backend": s.conf.BackendAddrs[idx]}).Observe(float64(end.Microseconds()) / 1000)
            s.mutex.Lock()
            hashes[buf_idx] = hash.Hash
            s.mutex.Unlock()

            return nil;
        })
    }

    err = wg.Wait()
    if err != nil {
        log.Fatalf("Error: can't hash given data with error %v", err)
    }

    return &parhashpb.ParHashResp{Hashes: hashes}, nil
}

