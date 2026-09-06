/* The same allocation-free behavior fixture is compiled for host and i386. */
#include <stdint.h>
#include <reist/result.h>
#include <reist/optional.h>
#include <reist/span.h>
#include <reist/fixed_string.h>
#include <reist/fixed_vector.h>
#include <reist/unique_handle.h>
#ifndef REIST_CPP_TYPES_FREESTANDING
#include <stdio.h>
#endif

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)
struct Counts { int live, made, dead, moves, copies; };
Counts counts{};
int closed[16]{}, close_count;

struct alignas(64) Probe {
    const int stamp;
    int value;
    explicit Probe(int v) noexcept : stamp(v), value(v) { ++counts.live; ++counts.made; }
    Probe(const Probe& p) noexcept : stamp(p.stamp), value(p.value) {
        ++counts.live; ++counts.made; ++counts.copies;
    }
    Probe(Probe&& p) noexcept : stamp(p.stamp), value(p.value) {
        p.value = -1; ++counts.live; ++counts.made; ++counts.moves;
    }
    ~Probe() noexcept { --counts.live; ++counts.dead; }
    Probe& operator=(const Probe&) = delete;
    Probe& operator=(Probe&&) = delete;
    Probe* operator&() = delete; // storage must use actual address, not overload
};
struct MoveOnly : Probe {
    explicit MoveOnly(int v) noexcept : Probe(v) {}
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly(const MoveOnly&) = delete;
};
struct IntHandleTraits {
    static constexpr int invalid() noexcept { return -1; }
    static constexpr bool is_valid(int h) noexcept { return h >= 0; }
    static constexpr bool equal(int a, int b) noexcept { return a == b; }
    static void close(int h) noexcept { if (close_count < 16) closed[close_count++] = h; }
};
using Owner = reist::UniqueHandle<int, IntHandleTraits>;
struct GenerationHandle { unsigned slot, generation; };
unsigned generation_closes, fenced_closes;
struct GenerationTraits {
    static constexpr GenerationHandle invalid() noexcept { return {0, 0}; }
    static constexpr bool is_valid(GenerationHandle h) noexcept { return h.generation != 0; }
    static constexpr bool equal(GenerationHandle a, GenerationHandle b) noexcept {
        return a.slot == b.slot && a.generation == b.generation;
    }
    static void close(GenerationHandle h) noexcept {
        ++generation_closes;
        // Model a bounded release-error adapter consuming cleanup via fencing,
        // not retrying or claiming that a destructor repaired a service.
        if (h.generation == 1) ++fenced_closes;
    }
};
static_assert(!__is_constructible(Owner, const Owner&));
static_assert(!__is_constructible(reist::Optional<MoveOnly>, const reist::Optional<MoveOnly>&));
static_assert(!__is_constructible(reist::FixedVector<MoveOnly, 2>, const reist::FixedVector<MoveOnly, 2>&));
static_assert(!__is_constructible(reist::Result<MoveOnly, int>, const reist::Result<MoveOnly, int>&));
static_assert(__is_constructible(reist::Span<const int>, int (&)[2]));
static_assert(!__is_constructible(reist::Span<int>, const int (&)[2]));
struct Base { int n; }; struct Derived : Base { int m; };
static_assert(!__is_constructible(reist::Span<Base>, Derived (&)[2]));

static int optional_test() {
    {
        reist::Optional<Probe> a;
        CHECK(!a && !a.get() && counts.live == 0);
        CHECK(a.try_emplace(7) && a.get()->stamp == 7);
        CHECK(reinterpret_cast<uintptr_t>(a.get()) % 64 == 0);
        int made = counts.made;
        CHECK(!a.try_emplace(8) && counts.made == made && a.get()->value == 7);
        reist::Optional<Probe> b(a), c(reist::move(a));
        CHECK(a.has_value() && a.get()->value == -1 && b.get()->value == 7 && c.get()->value == 7);
        auto* same = &b; b = *same; b = reist::move(*same);
        CHECK(b.get()->value == 7 && counts.live == 3);
        b = c; CHECK(b.get()->stamp == 7 && counts.live == 3);
        c.reset(); c.reset(); CHECK(!c && !c.get() && counts.live == 2);
        b = c; CHECK(!b && counts.live == 1);
        c = reist::move(a); CHECK(c && a && counts.live == 2);
        c.reset(); CHECK(c.try_emplace(9) && c.get()->stamp == 9);
        reist::Optional<MoveOnly> unique; CHECK(unique.try_emplace(12));
        reist::Optional<MoveOnly> moved(reist::move(unique));
        CHECK(unique && unique.get()->value == -1 && moved.get()->value == 12);
    }
    CHECK(counts.live == 0 && counts.made == counts.dead);
    return 0;
}

static int result_test() {
    {
        using R = reist::Result<Probe, Probe>;
        auto a = R::success(1), b = R::failure(2);
        CHECK(a && a.value_if()->stamp == 1 && !a.error_if());
        CHECK(!b && !b.value_if() && b.error_if()->stamp == 2);
        CHECK(reinterpret_cast<uintptr_t>(b.error_if()) % 64 == 0);
        R c(a), d(reist::move(b));
        CHECK(!b && b.error_if()->value == -1 && d.error_if()->value == 2);
        a = d; CHECK(!a && a.error_if()->value == 2);
        d = c; CHECK(d && d.value_if()->value == 1);
        auto* same = &d; d = *same; d = reist::move(*same);
        CHECK(d.value_if()->value == 1);
        c = reist::move(a); CHECK(!c && !a && c.error_if()->value == 2);
        using M = reist::Result<MoveOnly, int>;
        auto m = M::success(5); M n(reist::move(m));
        CHECK(n.value_if()->value == 5 && m.value_if()->value == -1);
        n = M::failure(22); CHECK(!n && *n.error_if() == 22);
        using V = reist::Result<void, MoveOnly>;
        auto ok = V::success(), error = V::failure(6);
        CHECK(ok && !ok.error_if() && !error && error.error_if()->value == 6);
        ok = reist::move(error); CHECK(!ok && !error && error.error_if()->value == -1);
        ok = V::success(); CHECK(ok);
        using VI = reist::Result<void, int>;
        auto vi = VI::failure(3); auto vj = VI::success(); vj = vi;
        CHECK(!vj && *vj.error_if() == 3);
    }
    CHECK(counts.live == 0 && counts.made == counts.dead);
    return 0;
}

static int span_test() {
    int storage[]{1, 2, 3};
    reist::Span<int> all(storage), output;
    CHECK(all.size() == 3 && *all.at(2) == 3 && !all.at(3) && !all.at(SIZE_MAX));
    CHECK(!output.data() && !output.at(0));
    CHECK(all.subspan(1, 2, output) && output.data() == storage + 1);
    CHECK(!all.subspan(SIZE_MAX, 1, output) && output.size() == 2);
    CHECK(!all.subspan(2, SIZE_MAX, output) && output.data() == storage + 1);
    CHECK(!reist::Span<int>::try_from(nullptr, 1, output) && output.size() == 2);
    CHECK(!reist::Span<int>::try_from(storage, SIZE_MAX, output));
    CHECK(!reist::Span<int>::try_from(reinterpret_cast<int*>(UINTPTR_MAX - 3), 1, output));
    CHECK(!reist::Span<int>::try_from(reinterpret_cast<int*>(uintptr_t(1)), 1, output));
    CHECK(all.subspan(3, 0, output) && output.empty() && output.data() == storage + 3);
    CHECK(reist::Span<int>::try_from(nullptr, 0, output) && !output.data());
    CHECK(output.subspan(0, 0, output) && !output.data());
    reist::Span<const int> read_only(all); CHECK(*read_only.at(0) == 1);
    CHECK(all.subspan(1, 2, all) && all.size() == 2 && *all.at(0) == 2);
    return 0;
}

static int string_test() {
    reist::FixedString<6> text;
    CHECK(text.empty() && text.capacity() == 6 && text.c_str()[0] == 0);
    CHECK(text.assign("abc") && text.append(text.view()) && text.size() == 6);
    CHECK(text.c_str()[5] == 'c' && !text.append("x") && text.size() == 6);
    reist::Span<const char> sub;
    CHECK(text.view().subspan(2, 3, sub));
    CHECK(text.assign(sub) && text.size() == 3 && text.c_str()[0] == 'c' && text.c_str()[3] == 0);
    auto copy = text; auto moved = reist::move(text);
    CHECK(copy.size() == 3 && moved.size() == 3 && text.empty() && text.c_str()[0] == 0);
    auto* same = &moved; moved = reist::move(*same); CHECK(moved.size() == 3);
    CHECK(!moved.assign("toolong") && moved.size() == 3);
    const char unterminated[]{'x', 'y'};
    CHECK(!moved.assign(unterminated) && moved.size() == 3);
    const char binary[]{'x', 0, 'y'};
    CHECK(moved.assign(reist::Span<const char>(binary)) && moved.size() == 3 && moved.c_str()[2] == 'y');
    moved.clear(); CHECK(moved.empty() && moved.c_str()[0] == 0);
    reist::FixedString<0> zero; CHECK(zero.assign("") && !zero.append("x"));
    return 0;
}

static int vector_test() {
    {
        reist::FixedVector<Probe, 2> a;
        CHECK(a.empty() && !a.at(0) && !a.pop_back() && counts.live == 0);
        CHECK(a.try_emplace_back(1) && a.try_emplace_back(2));
        CHECK(a.size() == 2 && a.capacity() == 2 && !a.at(2) && !a.at(SIZE_MAX));
        CHECK(reinterpret_cast<uintptr_t>(a.at(1)) % 64 == 0);
        int made = counts.made;
        CHECK(!a.try_emplace_back(3) && counts.made == made);
        reist::FixedVector<Probe, 2> b(a), c(reist::move(a));
        CHECK(a.empty() && b.at(0)->value == 1 && c.at(1)->value == 2);
        auto* same = &b; b = *same; b = reist::move(*same); CHECK(b.size() == 2);
        b = c; CHECK(b.at(1)->stamp == 2);
        CHECK(c.pop_back() && c.size() == 1 && !c.at(1));
        a = reist::move(c); CHECK(c.empty() && a.size() == 1);
        CHECK(a.try_emplace_back(8) && a.at(1)->stamp == 8);
        reist::FixedVector<MoveOnly, 1> unique;
        MoveOnly value(17); CHECK(unique.try_emplace_back(reist::move(value)));
        CHECK(!unique.try_emplace_back(reist::move(*unique.at(0))) && unique.at(0)->value == 17);
        reist::FixedVector<MoveOnly, 1> moved(reist::move(unique));
        CHECK(unique.empty() && moved.at(0)->value == 17);
        reist::FixedVector<Probe, 0> zero;
        made = counts.made; CHECK(!zero.try_emplace_back(1) && counts.made == made && !zero.pop_back());
    }
    CHECK(counts.live == 0 && counts.made == counts.dead);
    return 0;
}

static int handle_test() {
    {
        Owner empty; CHECK(!empty && empty.get() == -1);
        Owner a(0); CHECK(a && a.get() == 0);
        Owner b(reist::move(a)); CHECK(!a && b.get() == 0);
        auto* same = &b; b = reist::move(*same); b.reset(b.get());
        CHECK(close_count == 0 && b.get() == 0);
        Owner c(1); c = reist::move(b);
        CHECK(!b && c.get() == 0 && close_count == 1 && closed[0] == 1);
        int raw = c.release(); CHECK(raw == 0 && !c && close_count == 1);
        empty.reset(raw); empty.reset(2);
        CHECK(close_count == 2 && closed[1] == 0);
        empty.reset(); empty.reset(); CHECK(close_count == 3 && closed[2] == 2);
        Owner end(3);
    }
    CHECK(close_count == 4 && closed[3] == 3);
    {
        reist::UniqueHandle<GenerationHandle, GenerationTraits> a({7, 1});
        a.reset({7, 2});
        CHECK(generation_closes == 1 && fenced_closes == 1 && a.get().generation == 2);
        a.reset({7, 2}); CHECK(generation_closes == 1);
    }
    CHECK(generation_closes == 2 && fenced_closes == 1);
    return 0;
}

static int repeated_state_test() {
    reist::FixedVector<int, 8> values;
    int reference[8]{};
    size_t size = 0;
    uint32_t random = 0x12345678;
    for (unsigned step = 0; step < 4096; ++step) {
        random = random * 1664525U + 1013904223U;
        switch (random >> 30) {
        case 0: {
            int value = static_cast<int>(random & 0x7fffffffU);
            int* result = values.try_emplace_back(value);
            CHECK(bool(result) == (size < 8));
            if (size < 8) reference[size++] = value;
            break;
        }
        case 1:
            CHECK(values.pop_back() == (size != 0));
            if (size) --size;
            break;
        case 2: {
            auto moved = reist::move(values); CHECK(values.empty());
            values = reist::move(moved); CHECK(moved.empty());
            break;
        }
        default: {
            auto copy = values; values = copy;
            break;
        }
        }
        CHECK(values.size() == size && !values.at(size));
        for (size_t i = 0; i < size; ++i) CHECK(*values.at(i) == reference[i]);
    }
    return 0;
}

#ifdef REIST_CPP_TYPES_FREESTANDING
extern "C"
#endif
int main(int, char**) {
    int result = optional_test();
    if (!result) result = result_test();
    if (!result) result = span_test();
    if (!result) result = string_test();
    if (!result) result = vector_test();
    if (!result) result = handle_test();
    if (!result) result = repeated_state_test();
#ifndef REIST_CPP_TYPES_FREESTANDING
    if (result) printf("CPP_TYPES_FAIL line=%d\n", result);
    else puts("REIST_CPP_TYPES_HOST_OK");
#endif
    return result;
}
