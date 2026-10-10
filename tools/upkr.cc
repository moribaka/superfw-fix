// upkr.cpp -- single file C++ implementation of the upkr compression format.
//
// This is mostly derived from https://github.com/exoticorn/upkr using an LLM.
// Some tweaks and optimizations have been added, including some extra levels.
// Greedy compression (level 0) has been dropped altoghether.
//
// Public Domain - David Guillen Fandos <david@davidgf.net>
//
// Usage: upkr [-l LEVEL] [-d] <infile> [<outfile>]

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

static const uint32_t PROB_BITS = 8;
static const uint32_t ONE_PROB = 1u << PROB_BITS;  // 256
static const uint32_t UPDATE_RATE = 4;
static const uint32_t UPDATE_ADD = 8;
static const uint8_t INIT_PROB = (uint8_t)(1u << (PROB_BITS - 1));

// context layout (parity_contexts == 1)
static const size_t NCTX = 256 + 1 + 64 + 64;  // 385
static const size_t CTX_IS_MATCH = 0;          // + literal tree in 1..255
static const size_t CTX_NEW_OFFSET = 256;
static const size_t CTX_OFFSET = 257;  // 64 slots
static const size_t CTX_LENGTH = 321;  // 64 slots

static inline void update_prob(uint8_t &p, bool bit) {
    uint32_t old = p;
    if (bit)
        p = (uint8_t)(old + ((ONE_PROB - old + UPDATE_ADD) >> UPDATE_RATE));
    else
        p = (uint8_t)(old - ((old + UPDATE_ADD) >> UPDATE_RATE));
}

struct CoderState {
    uint8_t ctx[NCTX];
    uint32_t last_offset;
    bool prev_was_match;

    // Left uninitialised on purpose: hot paths always overwrite by copy.
    CoderState() {}
    void reset() {
        memset(ctx, INIT_PROB, sizeof(ctx));
        last_offset = 0;
        prev_was_match = false;
    }
};

// --------------------------------------------------------------- coders ----

// Records (prob, bit) pairs, then renders them into a rANS stream backwards.
struct RansCoder {
    std::vector<uint16_t> bits;

    void encode_bit(bool bit, uint16_t prob) {
        bits.push_back((uint16_t)(prob | ((uint16_t)bit << 15)));
    }

    std::vector<uint8_t> finish() {
        std::vector<uint8_t> buf;
        const uint32_t l_bits = 12;
        const uint32_t max_state_factor = 1u << (l_bits + 8 - PROB_BITS);
        uint32_t state = 1u << l_bits;

        for (size_t i = bits.size(); i-- > 0;) {
            uint32_t step = bits[i];
            uint32_t prob = step & 32767, start;
            if (step & 32768) {
                start = 0;
            } else {
                start = prob;
                prob = ONE_PROB - prob;
            }
            uint32_t max_state = max_state_factor * prob;
            while (state >= max_state) {
                buf.push_back((uint8_t)state);
                state >>= 8;
            }
            state = ((state / prob) << PROB_BITS) + (state % prob) + start;
        }
        while (state > 0) {
            buf.push_back((uint8_t)state);
            state >>= 8;
        }
        std::reverse(buf.begin(), buf.end());
        return buf;
    }
};

// Cost in bits of coding a bit at a given probability. Indexed by the
// probability of the symbol actually coded, so entry 0 is unused (context
// probabilities are clamped to [7, 249] by the update rule).
static double g_cost_table[ONE_PROB];

static void init_cost_table() {
    for (uint32_t p = 0; p < ONE_PROB; ++p)
        g_cost_table[p] = std::log2((double)ONE_PROB / (double)p);
}


// len == 0 -> literal, `off` holds the byte. Otherwise a match.
struct Op {
    uint32_t off;
    uint32_t len;
};

// One definition of the wire format, parameterised by a sink.
//
// Sink::UPDATE == false means "evaluate only": the state is read but never
// written. That is sound because no context index is visited twice within a
// single op -- the literal tree walks disjoint power-of-two bands, and the
// offset/length chains step +2 from disjoint bases (a 32-bit value reaches at
// most base+62, so the offset chain tops out at 319 and never touches the
// length chain at 321). Checked by assert_unique_contexts() in debug builds.

template <class S>
static inline void wbit(S &sink, CoderState &s, size_t ctx, bool bit) {
    sink.bit(ctx, s.ctx[ctx], bit);
    if (S::UPDATE) update_prob(s.ctx[ctx], bit);
}

template <class S>
static void wlength(S &sink, CoderState &s, size_t ctx, uint32_t value) {
    while (value >= 2) {
        wbit(sink, s, ctx, true);
        wbit(sink, s, ctx + 1, (value & 1) != 0);
        ctx += 2;
        value >>= 1;
    }
    wbit(sink, s, ctx, false);
}

template <class S>
static void walk_op(S &sink, CoderState &s, const Op &op) {
    if (op.len == 0) {
        wbit(sink, s, CTX_IS_MATCH, false);
        size_t ci = 1;
        for (int i = 7; i >= 0; --i) {
            bool bit = ((op.off >> i) & 1) != 0;
            wbit(sink, s, ci, bit);
            ci = (ci << 1) | (size_t)bit;
        }
        if (S::UPDATE) s.prev_was_match = false;
    } else {
        wbit(sink, s, CTX_IS_MATCH, true);
        bool new_offset = true;
        if (!s.prev_was_match) {
            new_offset = op.off != s.last_offset;
            wbit(sink, s, CTX_NEW_OFFSET, new_offset);
        }
        if (new_offset) {
            wlength(sink, s, CTX_OFFSET, op.off + 1);
            if (S::UPDATE) s.last_offset = op.off;
        }
        wlength(sink, s, CTX_LENGTH, op.len);
        if (S::UPDATE) s.prev_was_match = true;
    }
}

template <class S>
static void walk_eof(S &sink, CoderState &s) {
    wbit(sink, s, CTX_IS_MATCH, true);
    if (!s.prev_was_match) wbit(sink, s, CTX_NEW_OFFSET, true);
    wlength(sink, s, CTX_OFFSET, 1);
}

// Accumulates cost in bits, leaves the state untouched.
struct CostSink {
    static const bool UPDATE = false;
    double cost;
    CostSink() : cost(0.0) {}
    void bit(size_t, uint8_t prob, bool b) {
        cost += g_cost_table[b ? prob : ONE_PROB - prob];
    }
};

// Advances the state, produces nothing.
struct ApplySink {
    static const bool UPDATE = true;
    void bit(size_t, uint8_t, bool) {}
};

// Advances the state and emits to the rANS coder.
struct RansSink {
    static const bool UPDATE = true;
    RansCoder *coder;
    explicit RansSink(RansCoder &c) : coder(&c) {}
    void bit(size_t, uint8_t prob, bool b) { coder->encode_bit(b, prob); }
};

// Cost of `op` from state `s`, without disturbing `s`. The const_cast is safe:
// CostSink::UPDATE is false, so walk_op performs no writes.
static inline double cost_op(const CoderState &s, const Op &op) {
    CostSink sink;
    walk_op(sink, const_cast<CoderState &>(s), op);
    return sink.cost;
}

static inline void apply_op(CoderState &s, const Op &op) {
    ApplySink sink;
    walk_op(sink, s, op);
}

static inline void encode_op(RansCoder &coder, CoderState &s, const Op &op) {
    RansSink sink(coder);
    walk_op(sink, s, op);
}

static inline void encode_eof(RansCoder &coder, CoderState &s) {
    RansSink sink(coder);
    walk_eof(sink, s);
}

// O(n log n) prefix doubling with counting sort.
static std::vector<int32_t> suffix_array(const uint8_t *s, size_t n) {
    std::vector<int32_t> sa(n), rnk(n), tmp(n);
    if (n == 0) return sa;
    std::vector<int32_t> cnt(std::max<size_t>(256, n) + 1, 0);

    for (size_t i = 0; i < n; ++i) cnt[s[i]]++;
    for (int i = 1; i < 256; ++i) cnt[i] += cnt[i - 1];
    for (size_t i = n; i-- > 0;) sa[--cnt[s[i]]] = (int32_t)i;
    rnk[sa[0]] = 0;
    for (size_t i = 1; i < n; ++i)
        rnk[sa[i]] = rnk[sa[i - 1]] + (s[sa[i]] != s[sa[i - 1]] ? 1 : 0);

    for (size_t k = 1; (size_t)rnk[sa[n - 1]] != n - 1; k <<= 1) {
        size_t p = 0;
        for (size_t i = n - std::min(k, n); i < n; ++i) tmp[p++] = (int32_t)i;
        for (size_t i = 0; i < n; ++i)
            if ((size_t)sa[i] >= k) tmp[p++] = sa[i] - (int32_t)k;

        int32_t classes = rnk[sa[n - 1]] + 1;
        std::fill(cnt.begin(), cnt.begin() + classes, 0);
        for (size_t i = 0; i < n; ++i) cnt[rnk[i]]++;
        for (int32_t i = 1; i < classes; ++i) cnt[i] += cnt[i - 1];
        for (size_t i = n; i-- > 0;) sa[--cnt[rnk[tmp[i]]]] = tmp[i];

        tmp[sa[0]] = 0;
        for (size_t i = 1; i < n; ++i) {
            int32_t a = sa[i], b = sa[i - 1];
            int32_t ka = (size_t)a + k < n ? rnk[a + k] : -1;
            int32_t kb = (size_t)b + k < n ? rnk[b + k] : -1;
            tmp[a] = tmp[b] + ((rnk[a] == rnk[b] && ka == kb) ? 0 : 1);
        }
        rnk.swap(tmp);
    }
    return sa;
}

// --------------------------------------------------------- match finder ----

struct Match {
    size_t pos;
    size_t length;
};

struct Matches;

struct MatchFinder {
    size_t n;
    std::vector<int32_t> sa;
    std::vector<uint32_t> rev;
    std::vector<uint32_t> lcp;
    std::vector<size_t> queue;  // max-heap

    // Always overridden by the parser; these are just safe defaults.
    size_t max_queue_size = 100;
    size_t max_matches_per_length = 5;
    size_t patience = 100;
    size_t max_length_diff = 2;

    MatchFinder(const uint8_t *data, size_t n_) : n(n_) {
        sa = suffix_array(data, n);
        rev.assign(n, 0);
        for (size_t i = 0; i < n; ++i) rev[sa[i]] = (uint32_t)i;

        lcp.assign(n, 0);
        size_t length = 0;
        for (size_t p = 0; p < n; ++p) {
            size_t r = rev[p];
            if (r + 1 < n) {
                size_t i = sa[r], j = sa[r + 1];
                while (i + length < n && j + length < n &&
                       data[i + length] == data[j + length])
                    length++;
                lcp[r] = (uint32_t)length;
            }
            if (length) length--;
        }
    }

    Matches matches(size_t pos);
};

struct Matches {
    MatchFinder *f;
    size_t pos;  // valid match positions are 0..pos
    size_t left_index, left_length;
    size_t right_index, right_length;
    size_t current_length, matches_left, max_length;

    void move_left() {
        size_t pat = f->patience;
        while (left_length > 0 && pat > 0 && left_index > 0) {
            left_index--;
            left_length = std::min(left_length, (size_t)f->lcp[left_index]);
            if ((size_t)f->sa[left_index] < pos) return;
            pat--;
        }
        left_length = 0;
    }

    void move_right() {
        size_t pat = f->patience;
        while (right_length > 0 && pat > 0 && right_index + 1 < f->n) {
            right_index++;
            right_length = std::min(right_length, (size_t)f->lcp[right_index - 1]);
            if ((size_t)f->sa[right_index] < pos) return;
            pat--;
        }
        right_length = 0;
    }

    void push(size_t v) {
        f->queue.push_back(v);
        std::push_heap(f->queue.begin(), f->queue.end());
    }

    bool next(Match &out) {
        if (f->queue.empty() || matches_left == 0) {
            f->queue.clear();
            size_t dec = current_length > 0 ? current_length - 1 : 0;
            current_length = std::min(dec, std::max(left_length, right_length));
            max_length = std::max(max_length, current_length);
            if (current_length < 2 ||
                current_length + f->max_length_diff < max_length)
                return false;
            while (f->queue.size() < f->max_queue_size &&
                   (left_length == current_length || right_length == current_length)) {
                if (left_length == current_length) {
                    push((size_t)f->sa[left_index]);
                    move_left();
                }
                if (right_length == current_length) {
                    push((size_t)f->sa[right_index]);
                    move_right();
                }
            }
            matches_left = f->max_matches_per_length;
        }
        if (matches_left > 0) matches_left--;
        if (f->queue.empty()) return false;
        std::pop_heap(f->queue.begin(), f->queue.end());
        out.pos = f->queue.back();
        f->queue.pop_back();
        out.length = current_length;
        return true;
    }
};

Matches MatchFinder::matches(size_t pos) {
    size_t index = rev[pos];
    queue.clear();
    Matches m;
    m.f = this;
    m.pos = pos;
    m.left_index = index;
    m.left_length = SIZE_MAX;
    m.right_index = index;
    m.right_length = SIZE_MAX;
    m.current_length = SIZE_MAX;
    m.matches_left = 0;
    m.max_length = 0;
    m.move_left();
    m.move_right();
    return m;
}

struct MatchCache {
    Matches it;
    std::vector<Match> v;
    bool done;

    explicit MatchCache(const Matches &m) : it(m), done(false) {}

    bool get(size_t i, Match &out) {
        while (v.size() <= i && !done) {
            Match m;
            if (it.next(m))
                v.push_back(m);
            else
                done = true;
        }
        if (i >= v.size()) return false;
        out = v[i];
        return true;
    }
};

static inline size_t match_length_at(const uint8_t *data, size_t n, size_t pos,
                                     size_t offset) {
    size_t l = 0;
    while (pos + l < n && data[pos + l] == data[pos - offset + l]) l++;
    return l;
}

// Backwards-linked list of ops. Pooled, non-atomic refcounting, freed
// iteratively (the chains get long enough to blow the stack otherwise).
struct Parse {
    Parse *prev;
    uint32_t rc;
    Op op;
};

static std::vector<Parse *> g_parse_blocks;
static Parse *g_parse_free = nullptr;

static Parse *parse_alloc(Parse *prev, Op op) {
    if (!g_parse_free) {
        const size_t N = 16384;
        Parse *block = (Parse *)malloc(N * sizeof(Parse));
        g_parse_blocks.push_back(block);
        for (size_t i = 0; i < N; ++i) {
            block[i].prev = g_parse_free;
            g_parse_free = &block[i];
        }
    }
    Parse *p = g_parse_free;
    g_parse_free = p->prev;
    p->prev = prev;
    p->rc = 1;
    p->op = op;
    if (prev) prev->rc++;
    return p;
}

static void parse_release(Parse *p) {
    while (p && --p->rc == 0) {
        Parse *nxt = p->prev;
        p->prev = g_parse_free;
        g_parse_free = p;
        p = nxt;
    }
}

struct PRef {
    Parse *p;
    PRef() : p(nullptr) {}
    explicit PRef(Parse *q) : p(q) {}
    PRef(const PRef &o) : p(o.p) {
        if (p) p->rc++;
    }
    PRef(PRef &&o) noexcept : p(o.p) { o.p = nullptr; }
    PRef &operator=(const PRef &o) {
        if (this != &o) {
            if (o.p) o.p->rc++;
            parse_release(p);
            p = o.p;
        }
        return *this;
    }
    PRef &operator=(PRef &&o) noexcept {
        if (this != &o) {
            parse_release(p);
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
    ~PRef() { parse_release(p); }
};

// Materialized context states, shared between an arrival and all the candidate
// arrivals derived from it. Same pooled non-atomic refcounting as Parse, but
// without the chain, so release is O(1).
struct SState {
    CoderState st;
    uint32_t rc;
    SState *nextfree;
};

static std::vector<SState *> g_state_blocks;
static SState *g_state_free = nullptr;

static SState *state_alloc() {
    if (!g_state_free) {
        const size_t N = 256;
        SState *block = (SState *)malloc(N * sizeof(SState));
        g_state_blocks.push_back(block);
        for (size_t i = 0; i < N; ++i) {
            block[i].nextfree = g_state_free;
            g_state_free = &block[i];
        }
    }
    SState *s = g_state_free;
    g_state_free = s->nextfree;
    s->rc = 1;
    return s;
}

static inline void state_release(SState *s) {
    if (s && --s->rc == 0) {
        s->nextfree = g_state_free;
        g_state_free = s;
    }
}

struct SRef {
    SState *p;
    SRef() : p(nullptr) {}
    explicit SRef(SState *q) : p(q) {}
    SRef(const SRef &o) : p(o.p) {
        if (p) p->rc++;
    }
    SRef(SRef &&o) noexcept : p(o.p) { o.p = nullptr; }
    SRef &operator=(const SRef &o) {
        if (this != &o) {
            if (o.p) o.p->rc++;
            state_release(p);
            p = o.p;
        }
        return *this;
    }
    SRef &operator=(SRef &&o) noexcept {
        if (this != &o) {
            state_release(p);
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
    ~SRef() { state_release(p); }
};

// An arrival does not own a context state. It holds its *parent's* state plus
// the op that transforms it, so the 385-byte array is only built if and when
// this arrival is popped and survives the cost filter -- most never are.
// last_offset/prev_was_match are derivable without touching the array, and the
// filter and the sort need only those two plus the cost.
struct Arrival {
    PRef parse;
    SRef pstate;  // parent's state; for the root, its own
    Op op;
    bool has_op;  // false only for the root arrival
    uint32_t last_offset;
    bool prev_was_match;
    double cost;
};

// Build this arrival's own state. Cheap for the root, one copy + apply
// otherwise.
static SRef materialize(const Arrival &a) {
    if (!a.has_op) return a.pstate;
    SState *s = state_alloc();
    s->st = a.pstate.p->st;
    apply_op(s->st, a.op);
    return SRef(s);
}

struct LevelConfig {
    size_t max_arrivals;
    double max_cost_delta;
    double max_offset_cost_delta;
    size_t num_near_matches;
    size_t greedy_size;
    size_t max_queue_size;
    size_t patience;
    size_t max_matches_per_length;
    size_t max_length_diff;

    static LevelConfig from_level(unsigned level) {
        LevelConfig c;
        c.max_arrivals = level <= 1   ? 0
                         : level == 2 ? 2
                         : level == 3 ? 4
                         : level == 4 ? 8
                         : level == 5 ? 16
                         : level == 6 ? 32
                         : level == 7 ? 64
                         : level == 8 ? 96
                         : level == 9 ? 128
                                      : 256;
        c.max_cost_delta = 16.0;
        c.max_offset_cost_delta = level <= 4 ? 0.0
                                 : level <= 8 ? 4.0
                                 : level == 9 ? 8.0
                                              : 16.0;
        c.num_near_matches = level > 0 ? level - 1 : 0;
        c.greedy_size = 4 + (size_t)level * level * 3;
        c.max_length_diff = level <= 1   ? 0
                            : level <= 3 ? 1
                            : level <= 5 ? 2
                            : level <= 7 ? 3
                            : level <= 9 ? 4
                                         : 5;
        c.max_queue_size = (size_t)level * 256;
        c.patience = (size_t)level * 256;
        c.max_matches_per_length = level;
        return c;
    }
};

static void sort_arrivals(std::vector<Arrival> &v, size_t max_arrivals) {
    if (max_arrivals == 0) return;
    static std::vector<std::pair<double, uint32_t>> order;
    static std::vector<uint32_t> seen, chosen, remaining;
    static std::vector<Arrival> scratch;

    order.clear();
    for (uint32_t i = 0; i < v.size(); ++i) order.push_back({v[i].cost, i});
    std::stable_sort(order.begin(), order.end(),
                     [](const std::pair<double, uint32_t> &a,
                        const std::pair<double, uint32_t> &b) { return a.first < b.first; });

    seen.clear();
    chosen.clear();
    remaining.clear();
    for (size_t i = 0; i < order.size() && chosen.size() < max_arrivals; ++i) {
        uint32_t idx = order[i].second;
        uint32_t off = v[idx].last_offset;
        if (std::find(seen.begin(), seen.end(), off) == seen.end()) {
            seen.push_back(off);
            chosen.push_back(idx);
        } else {
            remaining.push_back(idx);
        }
    }
    for (size_t i = 0; i < remaining.size() && chosen.size() < max_arrivals; ++i)
        chosen.push_back(remaining[i]);

    scratch.clear();
    scratch.reserve(std::max(chosen.size(), max_arrivals * 2 + 1));
    for (size_t i = 0; i < chosen.size(); ++i) scratch.push_back(std::move(v[chosen[i]]));
    v.swap(scratch);
    scratch.clear();  // releases the arrivals that were dropped
}

typedef std::vector<std::vector<Arrival>> Arrivals;

static void add_arrival(Arrivals &arrivals, size_t pos, Arrival a,
                        size_t max_arrivals) {
    if (pos >= arrivals.size()) return;  // unreachable slot, never read
    std::vector<Arrival> &v = arrivals[pos];
    if (max_arrivals == 0) {
        if (v.empty())
            v.push_back(std::move(a));
        else if (v[0].cost > a.cost)
            v[0] = std::move(a);
        return;
    }
    v.reserve(max_arrivals * 2 + 1);
    v.push_back(std::move(a));
    if (v.size() > max_arrivals * 2) sort_arrivals(v, max_arrivals);
}

// Everything needed to expand one materialized arrival into candidates.
struct Expand {
    const CoderState *st;  // materialized state of the parent
    SRef sref;             // handed to every child
    Parse *parse;
    double cost;
};

static void add_child(Arrivals &arrivals, const Expand &e, size_t pos, Op op,
                      size_t max_arrivals) {
    Arrival a;
    a.parse = PRef(parse_alloc(e.parse, op));
    a.pstate = e.sref;
    a.op = op;
    a.has_op = true;
    a.last_offset = op.len ? op.off : e.st->last_offset;
    a.prev_was_match = op.len != 0;
    a.cost = e.cost + cost_op(*e.st, op);
    add_arrival(arrivals, pos, std::move(a), max_arrivals);
}

static void add_match(Arrivals &arrivals, const Expand &e, size_t pos,
                      size_t offset, size_t length, size_t max_arrivals) {
    if (length < 1 || pos + length >= arrivals.size()) return;
    Op op = {(uint32_t)offset, (uint32_t)length};
    add_child(arrivals, e, pos + length, op, max_arrivals);
}

static std::vector<uint8_t> pack_parsing(const uint8_t *data, size_t n,
                                         unsigned level) {
    LevelConfig cfg = LevelConfig::from_level(level);
    MatchFinder mf(data, n);
    mf.max_queue_size = cfg.max_queue_size;
    mf.patience = cfg.patience;
    mf.max_matches_per_length = cfg.max_matches_per_length;
    mf.max_length_diff = cfg.max_length_diff;

    std::vector<size_t> near_matches(1024, SIZE_MAX);
    std::vector<size_t> last_seen(256, SIZE_MAX);
    const size_t max_arrivals = cfg.max_arrivals;

    Arrivals arrivals(n + 1);
    {
        SState *root = state_alloc();
        root->st.reset();
        Arrival init;
        init.pstate = SRef(root);
        init.op = Op{0, 0};
        init.has_op = false;
        init.last_offset = 0;
        init.prev_was_match = false;
        init.cost = 0.0;
        add_arrival(arrivals, 0, std::move(init), max_arrivals);
    }

    for (size_t pos = 0; pos < n; ++pos) {
        if (arrivals[pos].empty()) continue;
        std::vector<Arrival> here;
        here.swap(arrivals[pos]);
        sort_arrivals(here, max_arrivals);

        // The match sequence for a position is deterministic, so enumerate it
        // lazily once and replay it for every arrival at this position.
        MatchCache mcache(mf.matches(pos));

        double best_cost = 1e308;
        std::vector<std::pair<uint32_t, double>> best_per_offset;
        for (const Arrival &a : here) {
            best_cost = std::min(best_cost, a.cost);
            bool found = false;
            for (auto &e : best_per_offset)
                if (e.first == a.last_offset) {
                    e.second = std::min(e.second, a.cost);
                    found = true;
                    break;
                }
            if (!found) best_per_offset.push_back({a.last_offset, a.cost});
        }

        for (size_t ai = 0; ai < here.size(); ++ai) {
            Arrival &arrival = here[ai];
            double per_offset = 0;
            for (auto &e : best_per_offset)
                if (e.first == arrival.last_offset) {
                    per_offset = e.second;
                    break;
                }
            if (arrival.cost > std::min(best_cost + cfg.max_cost_delta,
                                        per_offset + cfg.max_offset_cost_delta))
                continue;

            Expand e;
            e.sref = materialize(arrival);
            e.st = &e.sref.p->st;
            e.parse = arrival.parse.p;
            e.cost = arrival.cost;

            bool found_last_offset = false;
            bool has_closest = false;
            size_t closest_match = 0;
            bool greedy_break = false;

            Match m;
            for (size_t mi = 0; mcache.get(mi, m); ++mi) {
                closest_match = has_closest ? std::max(closest_match, m.pos) : m.pos;
                has_closest = true;
                size_t offset = pos - m.pos;
                if ((uint32_t)offset == e.st->last_offset)
                    found_last_offset = true;
                add_match(arrivals, e, pos, offset, m.length, max_arrivals);
                if (m.length >= cfg.greedy_size) {
                    greedy_break = true;
                    break;
                }
            }
            if (greedy_break) break;  // 'arrival_loop

            size_t near_left = cfg.num_near_matches;
            size_t match_pos = last_seen[data[pos]];
            while (near_left > 0 && match_pos != SIZE_MAX &&
                   (!has_closest || closest_match < match_pos)) {
                size_t offset = pos - match_pos;
                size_t length = match_length_at(data, n, pos, offset);
                add_match(arrivals, e, pos, offset, length, max_arrivals);
                if ((uint32_t)offset == e.st->last_offset)
                    found_last_offset = true;
                if (offset < near_matches.size())
                    match_pos = near_matches[match_pos % near_matches.size()];
                near_left--;
            }

            if (!found_last_offset && e.st->last_offset > 0) {
                size_t offset = e.st->last_offset;
                size_t length = match_length_at(data, n, pos, offset);
                if (length > 0)
                    add_match(arrivals, e, pos, offset, length, max_arrivals);
            }

            add_child(arrivals, e, pos + 1, Op{data[pos], 0}, max_arrivals);
        }

        near_matches[pos % near_matches.size()] = last_seen[data[pos]];
        last_seen[data[pos]] = pos;
    }

    if (arrivals[n].empty()) {
        fprintf(stderr, "internal error: no parse found\n");
        exit(1);
    }

    // Overflow-sorting leaves this vector only partially ordered, so element 0
    // is not necessarily the cheapest arrival. The reference takes it anyway;
    // we take the real minimum (see UPKR_REF_EXACT).
    size_t pick = 0;
    for (size_t i = 1; i < arrivals[n].size(); ++i)
        if (arrivals[n][i].cost < arrivals[n][pick].cost) pick = i;

    std::vector<Op> ops;
    for (Parse *p = arrivals[n][pick].parse.p; p; p = p->prev) ops.push_back(p->op);
    arrivals.clear();
    for (Parse *b : g_parse_blocks) free(b);
    for (SState *b : g_state_blocks) free(b);

    RansCoder coder;
    CoderState state;
    state.reset();
    for (size_t i = ops.size(); i-- > 0;) encode_op(coder, state, ops[i]);
    encode_eof(coder, state);
    return coder.finish();
}

struct RansDecoder {
    const uint8_t *data;
    size_t n, pos;
    uint32_t state;

    RansDecoder(const uint8_t *d, size_t n_) : data(d), n(n_), pos(0), state(0) {
        refill();
    }

    void refill() {
        while (state < 4096) {
            if (pos >= n) {
                fprintf(stderr, "error: unexpected end of input\n");
                exit(1);
            }
            state = (state << 8) | data[pos++];
        }
    }

    bool decode_bit(uint16_t p) {
        refill();
        uint32_t prob = p;
        bool bit = (state & (ONE_PROB - 1)) < prob;
        uint32_t start;
        if (bit) {
            start = 0;
        } else {
            start = prob;
            prob = ONE_PROB - prob;
        }
        state = prob * (state >> PROB_BITS) + (state & (ONE_PROB - 1)) - start;
        return bit;
    }

    bool decode(uint8_t *ctx, size_t i) {
        bool bit = decode_bit(ctx[i]);
        update_prob(ctx[i], bit);
        return bit;
    }

    size_t decode_length(uint8_t *ctx, size_t i) {
        size_t length = 0;
        int bit_pos = 0;
        while (decode(ctx, i)) {
            length |= (size_t)decode(ctx, i + 1) << bit_pos;
            if (++bit_pos >= 32) {
                fprintf(stderr, "error: value overflow\n");
                exit(1);
            }
            i += 2;
        }
        return length | ((size_t)1 << bit_pos);
    }
};

static std::vector<uint8_t> unpack(const uint8_t *data, size_t n,
                                   size_t max_size) {
    RansDecoder dec(data, n);
    uint8_t ctx[NCTX];
    memset(ctx, INIT_PROB, sizeof(ctx));

    std::vector<uint8_t> out;
    size_t offset = SIZE_MAX;
    bool prev_was_match = false;

    for (;;) {
        if (dec.decode(ctx, CTX_IS_MATCH)) {
            if (prev_was_match || dec.decode(ctx, CTX_NEW_OFFSET)) {
                offset = dec.decode_length(ctx, CTX_OFFSET) - 1;
                if (offset == 0) break;
            }
            size_t length = dec.decode_length(ctx, CTX_LENGTH);
            if (offset > out.size()) {
                fprintf(stderr, "error: match offset out of range: %zu > %zu\n",
                        offset, out.size());
                exit(1);
            }
            if (out.size() + length > max_size) {
                fprintf(stderr, "error: unpacked data over size limit\n");
                exit(1);
            }
            for (size_t i = 0; i < length; ++i) out.push_back(out[out.size() - offset]);
            prev_was_match = true;
        } else {
            size_t ci = 1;
            uint8_t byte = 0;
            for (int i = 7; i >= 0; --i) {
                bool bit = dec.decode(ctx, ci);
                ci = (ci << 1) | (size_t)bit;
                byte |= (uint8_t)bit << i;
            }
            if (out.size() >= max_size) {
                fprintf(stderr, "error: unpacked data over size limit\n");
                exit(1);
            }
            out.push_back(byte);
            prev_was_match = false;
        }
    }
    return out;
}

static std::vector<uint8_t> read_all(const std::string &path) {
    std::vector<uint8_t> buf;
    FILE *f = path == "-" ? stdin : fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open %s\n", path.c_str());
        exit(1);
    }
    uint8_t chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buf.insert(buf.end(), chunk, chunk + got);
    if (f != stdin) fclose(f);
    return buf;
}

static void write_all(const std::string &path, const std::vector<uint8_t> &d) {
    FILE *f = path == "-" ? stdout : fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "error: cannot create %s\n", path.c_str());
        exit(1);
    }
    if (!d.empty() && fwrite(d.data(), 1, d.size(), f) != d.size()) {
        fprintf(stderr, "error: write failed\n");
        exit(1);
    }
    if (f != stdout) fclose(f); else fflush(stdout);
}

static void usage(int code) {
    fprintf(stderr,
            "upkr - LZ+rANS packer (upkr default format)\n\n"
            "Usag<local-path-redacted>"
            "  upkr [-l LEVEL] <infile> [<outfile>]   compress\n"
            "  upkr -d <infile> [<outfile>]           decompress\n\n"
            "  -l, --level N    compression level 1-15 (default 2)\n"
            "                   marginally smaller\n"
            "  -1 .. -9         short form of --level\n"
            "  -d, -u           decompress\n"
            "  -h, --help       this help\n\n"
            "'-' or a missing filename means stdin/stdout.\n");
    exit(code);
}

int main(int argc, char **argv) {
    init_cost_table();

    unsigned level = 2;
    bool decompress = false;
    std::string infile, outfile;
    int files = 0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-d" || a == "-u" || a == "--decompress" || a == "--unpack") {
            decompress = true;
        } else if (a == "-h" || a == "--help") {
            usage(0);
        } else if (a == "-l" || a == "--level") {
            if (i + 1 >= argc) usage(1);
            level = (unsigned)atoi(argv[++i]);
        } else if (a.size() > 1 && a[0] == '-' &&
                   a.find_first_not_of("0123456789", 1) == std::string::npos) {
            level = (unsigned)atoi(a.c_str() + 1);
        } else if (a.size() > 1 && a[0] == '-' && a != "-") {
            fprintf(stderr, "error: unknown option %s\n", a.c_str());
            usage(1);
        } else {
            if (files == 0) infile = a;
            else if (files == 1) outfile = a;
            else usage(1);
            files++;
        }
    }
    if (level < 1 || level > 15) {
        fprintf(stderr, "error: level must be 1-15\n");
        return 1;
    }
    if (infile.empty()) infile = "-";

    if (outfile.empty()) {
        if (infile == "-") {
            outfile = "-";
        } else if (!decompress) {
            outfile = infile + ".upk";
        } else {
            if (infile.size() > 4 && infile.compare(infile.size() - 4, 4, ".upk") == 0)
                outfile = infile.substr(0, infile.size() - 4);
            else
                outfile = infile + ".bin";
        }
    }

    std::vector<uint8_t> data = read_all(infile);

    if (decompress) {
        std::vector<uint8_t> out = unpack(data.data(), data.size(), 512u << 20);
        write_all(outfile, out);
    } else {
        std::vector<uint8_t> out = pack_parsing(data.data(), data.size(), level);
        fprintf(stderr, "Compressed %zu bytes to %zu bytes (%.2f%%)\n", data.size(),
                out.size(),
                data.empty() ? 0.0 : out.size() * 100.0 / data.size());
        write_all(outfile, out);
    }
    return 0;
}

