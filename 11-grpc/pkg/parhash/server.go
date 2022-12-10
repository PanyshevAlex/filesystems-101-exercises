package parhash

import (
    "context"

    "github.com/pkg/errors"
    "golang.org/x/sync/semaphore"
    "google.golang.org/grpc"
	"fs101ex/pkg/workgroup"
    "log"
    "net"
    "sync"
    hashpb "fs101ex/pkg/gen/hashsvc"
    parhashpb "fs101ex/pkg/gen/parhashsvc"
)

type Config struct {
    ListenAddr   string
    BackendAddrs []string
    Concurrency  int
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.

// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.

// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:

// 	backend 0: buffers 0, 2, and 4,
// 	backend 1: buffers 1 and 3.

// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.

// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
type Server struct {
    conf Config

    sem *semaphore.Weighted

    mutex sync.Mutex
    globalCounter int

    stop context.CancelFunc
    l    net.Listener
    wg   sync.WaitGroup
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
        wg.Go(ctx, func(ctx context.Context) error {
            s.mutex.Lock()
            idx := s.globalCounter % len(client)
            s.globalCounter++
            hashReq := hashpb.HashReq{Data: buf}
            s.mutex.Unlock()
            hash, err := client[idx].Hash(ctx, &hashReq)
            if err != nil {
                return err
            }

            s.mutex.Lock()
            hashes[i] = hash.Hash
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
