# Writing objects

There are two kinds of extension points:

* **Engine objects** (C++) – anything that processes messages or signals.
* **Front ends** (QML) – what a box looks like and how you interact with it.
  A front end can also *be* the whole object, without any C++; see
  [Writing node front ends](WRITING_FRONTENDS.md).

This page is about engine objects.

## Anatomy

```cpp
#include "Common.hpp"   // src/core/objects/Common.hpp

class Counter final : public Object {
public:
    explicit Counter(const ObjectArgs& a) : Object(a), step_(argFloat(a.args, 0, 1)) {
        addInlet();          // 0: bang counts, float sets
        addInlet();          // 1: step
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0) {
            outFloat(0, count_);
            count_ += step_;
        }
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 0) count_ = f;
        else            step_ = f;
    }

private:
    double count_ = 0, step_;
};

// in the file's register function
r.add<Counter>("counter", "count bangs", {"cnt"});
```

* Iolets are declared in the constructor, in order. `addSignalInlet()` /
  `addSignalOutlet()` make signal iolets; a float sent to a signal inlet sets
  its *scalar*, which is what the inlet reads while nothing is connected.
* Override only the handlers you need: `onBang`, `onFloat`, `onSymbol`,
  `onList`, `onAnything`. Defaults follow Pd: a list into inlet 0 is spread
  across the inlets, unknown messages print "no method for ...".
* Output with `outBang/outFloat/outSymbol/outList/outAnything`. Messages are
  depth first, exactly like Pd.
* Throw `ObjectError("why")` from the constructor for bad arguments; the box
  is shown broken with that text.
* Use `error("x is {}", x)` / `info(...)` to print to the console – it uses
  `std::format` syntax and does not allocate.

## Threads – the one rule that matters

| Runs on the **control (GUI) thread**    | Runs on the **engine (audio) thread** |
|-----------------------------------------|---------------------------------------|
| constructor, destructor, `prepare()`    | every `on…` handler, `process()`, `uiEvent()`, `loadbang()`, `onActivate()`, `onDeactivate()`, clock callbacks |

The object is not visible to the engine while the control thread uses it, so
no locking is ever needed – but:

* **Do not allocate in engine-thread code** if you can avoid it. Reserve
  vectors in the constructor or in `prepare()`. (Rare, user-triggered
  operations like resizing an array may allocate.)
* Never block, never take a mutex, never do I/O on the engine thread.
* Creating a `Symbol` from a string takes a lock; create the symbols you
  compare against in the constructor and keep them as members.

## Signal processing

```cpp
class Gain final : public Object {
public:
    explicit Gain(const ObjectArgs& a) : Object(a) {
        addSignalInlet();
        addSignalInlet(static_cast<float>(argFloat(a.args, 0, 1)));  // scalar default
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0), g = io.in(1);
        auto out = io.out(0);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = in[i] * g[i];
    }
};
```

* `io.in(k)` / `io.out(k)` index *signal* iolets only, in declaration order.
* Inputs and outputs never alias – write plain loops; GCC vectorises them.
* Fan-in (several cords into one inlet) is summed for you.
* `prepare(const DspSetup&)` runs on the control thread when the object is
  created and whenever the sample rate changes: allocate delay lines there.
* An object with no signal iolets that still wants a per-block call
  overrides `wantsDsp()` to return true (see `[bang~]`).

## Time

`makeClock(callback)` returns a `Clock&` owned by the object. `clock.delay(ms)`
schedules in logical time – sample accurate and drift free, like Pd's
`clock_delay`. Clocks are unset automatically when the object is removed.

## Names

`makeBinding(Binding::Kind::Receiver, name)` attaches the object to a name;
it binds when the object becomes active and unbinds when it is removed.
`binding.rebind(newName)` changes it at runtime. Messages to a receiver
name arrive in `receiveNamed()` (default: inlet 0). `engine().deliverToName`
sends to every receiver of a name. Tables use `Binding::Kind::Table` and the
`TableStorage` interface (`Table.hpp`).

## Talking to the GUI

* `notifyUi(selector, args)` sends a value to the object's front end; QML
  sees it as `node.value` (for `float`/`symbol`/`list`) or the
  `node.message(selector, args)` signal.
* `uiEvent(selector, args)` receives interaction from the front end
  (default: same as a message to inlet 0).
* Bulk data (scopes, arrays) goes through a `SnapshotBuffer` returned from
  `snapshot()`; the GUI polls it once per frame.

## Testing

`tests/Harness.hpp` runs a Runtime synchronously. A typical test:

```cpp
TEST_CASE("[counter] counts") {
    Harness h;
    auto& c = h.open(R"(#N canvas 0 0 400 300 12;
#X obj 10 10 counter 2;
#X obj 10 40 print;
#X connect 0 0 1 0;
)");
    h.send(c.boxes()[0].get(), 0, "bang");
    h.send(c.boxes()[0].get(), 0, "bang");
    CHECK(h.take() == std::vector<std::string>{"print: 0", "print: 2"});
}
```
