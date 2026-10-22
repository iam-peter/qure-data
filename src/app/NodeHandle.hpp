#pragma once

#include "Atom.hpp"
#include "Object.hpp"

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace qure {
struct Box;
}

namespace qure::ui {

class PatchDocument;

/// The object a QML front end talks to: `node` inside every node component.
///
///   node.value        last value the engine reported (number, string or list)
///   node.args         creation arguments (after the class name)
///   node.send(v)      interact: a number, string or list goes to the object
///   node.output(i, v) for front-end-only objects: send v out of outlet i
///   node.message(selector, args)   signal for every engine notification
///   node.received(inlet, selector, args)  front-end-only objects: input
class NodeHandle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("NodeHandle is provided to node front ends as 'node'")

    Q_PROPERTY(QString className READ className NOTIFY argsChanged)
    Q_PROPERTY(QVariantList args READ args NOTIFY argsChanged)
    Q_PROPERTY(QVariant value READ value NOTIFY valueChanged)
    Q_PROPERTY(bool editMode READ editMode NOTIFY editModeChanged)
    Q_PROPERTY(bool selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QList<float> samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(int numInlets READ numInlets NOTIFY argsChanged)
    Q_PROPERTY(int numOutlets READ numOutlets NOTIFY argsChanged)
    /// Extra description for special boxes, e.g. array graphs:
    /// { array, size, x1, y1, x2, y2 }.
    Q_PROPERTY(QVariantMap info READ info NOTIFY argsChanged)

public:
    NodeHandle(PatchDocument& document, Box& box);

    [[nodiscard]] Box& box() const { return *box_; }
    [[nodiscard]] QString className() const;
    [[nodiscard]] QVariantList args() const;
    [[nodiscard]] QVariant value() const { return value_; }
    [[nodiscard]] bool editMode() const { return editMode_; }
    [[nodiscard]] bool selected() const { return selected_; }
    [[nodiscard]] QList<float> samples() const { return samples_; }
    [[nodiscard]] int numInlets() const;
    [[nodiscard]] int numOutlets() const;
    [[nodiscard]] QVariantMap info() const;

    Q_INVOKABLE void send(const QVariant& value);
    Q_INVOKABLE void sendMessage(const QString& selector, const QVariantList& args = {});
    Q_INVOKABLE void output(int outlet, const QVariant& value = {});
    Q_INVOKABLE void click();
    /// Changes a creation argument in place (persisted on save) without
    /// recreating the object – for front-end state such as a saved value.
    Q_INVOKABLE void setArg(int index, const QVariant& value);

    // Called by the GUI layer.
    void applyNotification(Symbol selector, AtomSpan args);
    void setEditMode(bool on);
    void setSelected(bool on);
    /// Polls the object's snapshot buffer; returns true if new data arrived.
    bool pollSamples();
    void argsUpdated() { emit argsChanged(); }

    static QVariant toVariant(AtomSpan atoms);
    static QVariant toVariant(const Atom& atom);
    static AtomVector toAtoms(const QVariant& value);

signals:
    void argsChanged();
    void valueChanged();
    void editModeChanged();
    void selectedChanged();
    void samplesChanged();
    void message(const QString& selector, const QVariantList& args);
    void received(int inlet, const QString& selector, const QVariantList& args);
    void bang();

private:
    PatchDocument& document_;
    Box* box_;
    QVariant value_;
    QList<float> samples_;
    std::vector<float> scratch_;
    std::uint64_t snapshotSeq_ = 0;
    bool editMode_ = false;
    bool selected_ = false;
};

} // namespace qure::ui
