#include "NodeHandle.hpp"

#include "PatchDocument.hpp"
#include "Session.hpp"

#include "Patch.hpp"
#include "Runtime.hpp"
#include "Snapshot.hpp"

#include <QUrl>

namespace qure::ui {

NodeHandle::NodeHandle(PatchDocument& document, Box& box) : QObject(&document), document_(document), box_(&box) {}

QString NodeHandle::className() const { return frontendKey(*box_); }

QVariantList NodeHandle::args() const {
    QVariantList out;
    // Atom boxes store their properties as the whole text; objects have
    // the class name first.
    const bool atomBox = box_->kind != BoxKind::Object;
    for (std::size_t i = atomBox ? 0 : 1; i < box_->text.size(); ++i)
        out << toVariant(box_->text[i]);
    return out;
}

int NodeHandle::numInlets() const { return box_->numInlets(); }
int NodeHandle::numOutlets() const { return box_->numOutlets(); }

QVariantMap NodeHandle::info() const {
    QVariantMap m;
    // Where relative file arguments (scene.qml, effect.frag) resolve.
    if (const auto dir = box_->canvas->directory(); !dir.empty())
        m.insert(QStringLiteral("dir"), QUrl::fromLocalFile(QString::fromStdString(dir.string()) + QLatin1Char('/')));
    if (box_->child) {
        const auto& p = box_->child->props;
        m.insert(QStringLiteral("x1"), p.x1);
        m.insert(QStringLiteral("y1"), p.y1);
        m.insert(QStringLiteral("x2"), p.x2);
        m.insert(QStringLiteral("y2"), p.y2);
        for (const auto& b : box_->child->boxes())
            if (b->kind == BoxKind::Array && !b->text.empty()) {
                m.insert(QStringLiteral("array"), QString::fromStdString(b->text[0].toString()));
                if (b->text.size() > 1)
                    m.insert(QStringLiteral("size"), b->text[1].asFloat());
                break;
            }
    }
    return m;
}

QVariant NodeHandle::toVariant(const Atom& a) {
    switch (a.type()) {
    case Atom::Type::Float: return a.asFloat();
    case Atom::Type::Symbol:
    case Atom::Type::DollarSymbol: return QString::fromUtf8(a.asSymbol().str());
    default: return QString::fromStdString(a.toString());
    }
}

QVariant NodeHandle::toVariant(AtomSpan atoms) {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(atoms.size()));
    for (const auto& a : atoms)
        list << toVariant(a);
    return list;
}

AtomVector NodeHandle::toAtoms(const QVariant& value) {
    AtomVector out;
    auto one = [&](const QVariant& v) {
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (v.typeId() == QMetaType::QString && !ok)
            out.emplace_back(Symbol(v.toString().toStdString()));
        else if (v.typeId() == QMetaType::Bool)
            out.emplace_back(v.toBool() ? 1.0 : 0.0);
        else
            out.emplace_back(ok ? d : 0.0);
    };
    if (value.typeId() == QMetaType::QVariantList || value.canConvert<QVariantList>()) {
        if (value.typeId() != QMetaType::QString) {
            for (const auto& v : value.toList())
                one(v);
            return out;
        }
    }
    if (value.isValid())
        one(value);
    return out;
}

void NodeHandle::send(const QVariant& value) {
    if (!box_->object)
        return;
    const auto atoms = toAtoms(value);
    Symbol selector = sym::list();
    if (atoms.empty())
        selector = sym::bang();
    else if (atoms.size() == 1)
        selector = atoms[0].isFloat() ? sym::float_() : sym::symbol();
    document_.session().runtime().engine().sendUiEvent(box_->object->id(), selector, atoms);
}

void NodeHandle::sendMessage(const QString& selector, const QVariantList& args) {
    if (!box_->object)
        return;
    const auto atoms = toAtoms(args);
    document_.session().runtime().engine().sendUiEvent(box_->object->id(), Symbol(selector.toStdString()), atoms);
}

void NodeHandle::output(int outlet, const QVariant& value) {
    if (!box_->object)
        return;
    AtomVector atoms{Atom(static_cast<double>(outlet))};
    const auto rest = toAtoms(value);
    atoms.insert(atoms.end(), rest.begin(), rest.end());
    document_.session().runtime().engine().sendUiEvent(box_->object->id(), Symbol("out"), atoms);
}

void NodeHandle::click() {
    if (box_->object)
        document_.session().runtime().engine().sendUiEvent(box_->object->id(), sym::bang(), {});
}

void NodeHandle::setArg(int index, const QVariant& value) {
    const auto atoms = toAtoms(value);
    if (atoms.empty() || index < 0)
        return;
    const std::size_t pos = static_cast<std::size_t>(index) + (box_->kind == BoxKind::Object ? 1 : 0);
    if (pos >= box_->text.size())
        box_->text.resize(pos + 1, Atom(0.0));
    if (box_->text[pos] == atoms[0])
        return;
    box_->text[pos] = atoms[0];
    document_.markModified();
    emit argsChanged();
}

void NodeHandle::applyNotification(Symbol selector, AtomSpan args) {
    const auto name = selector.str();
    if (name == "float" || name == "symbol") {
        value_ = args.empty() ? QVariant() : toVariant(args[0]);
        emit valueChanged();
    } else if (name == "list") {
        value_ = toVariant(args);
        emit valueChanged();
    } else if (name == "bang") {
        emit bang();
    } else if (name == "in" && args.size() >= 2) {
        const int inlet = static_cast<int>(args[0].asFloat());
        const QString sel = QString::fromUtf8(args[1].asSymbol().str());
        const auto rest = args.subspan(2);
        // Remember named values: "p0 0.5", "color 1 0 0", a float per inlet.
        if (sel == QLatin1String("float") && !rest.empty())
            params_.insert(QStringLiteral("in%1").arg(inlet), toVariant(rest[0]));
        else if (sel != QLatin1String("bang") && sel != QLatin1String("list") && rest.size() == 1)
            params_.insert(sel, toVariant(rest[0]));
        else if (sel != QLatin1String("bang") && sel != QLatin1String("list") && rest.size() > 1)
            params_.insert(sel, toVariant(rest));
        if (sel != QLatin1String("bang") && !rest.empty())
            emit paramsChanged();
        emit received(inlet, sel, toVariant(rest).toList());
    }
    emit message(QString::fromUtf8(name), toVariant(args).toList());
}

void NodeHandle::setEditMode(bool on) {
    if (editMode_ != on) {
        editMode_ = on;
        emit editModeChanged();
    }
}

void NodeHandle::setSelected(bool on) {
    if (selected_ != on) {
        selected_ = on;
        emit selectedChanged();
    }
}

bool NodeHandle::pollSamples() {
    const Object* obj = box_->object.get();
    if (!obj && box_->child) // array graphs: show the first array inside
        for (const auto& b : box_->child->boxes())
            if (b->kind == BoxKind::Array && b->object) {
                obj = b->object.get();
                break;
            }
    const SnapshotBuffer* snap = obj ? obj->snapshot() : nullptr;
    if (!snap || !snap->read(scratch_, snapshotSeq_))
        return false;
    samples_ = QList<float>(scratch_.begin(), scratch_.end());
    emit samplesChanged();
    return true;
}

} // namespace qure::ui
