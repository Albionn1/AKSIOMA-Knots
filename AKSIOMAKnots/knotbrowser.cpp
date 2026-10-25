#include "knotbrowser.h"

#include "theme.h"

#include "knotglwidget.h"
#include "knottable.h"

#include <QAbstractListModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

/*
 * The (filtered) list of knots: a vector of table indices.
 */
class KnotListModel final
    : public QAbstractListModel
{
public:
    explicit KnotListModel(QObject *parent)
        : QAbstractListModel(parent)
    {
    }

    void setRows(const QVector<int> &rows)
    {
        beginResetModel();
        rows_ = rows;
        endResetModel();
    }

    int tableIndex(int row) const
    {
        return row >= 0 && row < rows_.size() ? rows_.at(row) : -1;
    }

    int rowOf(int tableIndex) const
    {
        return rows_.indexOf(tableIndex);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role != Qt::DisplayRole || !index.isValid())
            return QVariant();

        const KnotMath::KnotTableRecord &record =
            KnotMath::KnotTable::instance().at(rows_.at(index.row()));

        const QString name = KnotMath::KnotTable::displayName(record.name);
        const QString described = KnotMath::KnotTable::descriptiveName(record);

        return described.isEmpty() ? name : name + QStringLiteral("   ·   ") + described;
    }

private:
    QVector<int> rows_;
};

namespace
{

// "8_19", "8 19", "819", "8₁₉" all become "819"; names are compared this way
QString searchKey(QString text)
{
    static const QString subscripts = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");

    QString key;

    for (const QChar c : text.toLower())
    {
        const int sub = subscripts.indexOf(c);

        if (sub >= 0)
            key += QChar('0' + sub);
        else if (c.isLetterOrNumber())
            key += c;
    }

    return key;
}

} // namespace

KnotBrowserDialog::KnotBrowserDialog(const QString &current, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Knot Browser"));
    resize(1080, 680);

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();
    QGuiApplication::restoreOverrideCursor();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    /*
     * Search and filters
     */
    QHBoxLayout *filters = new QHBoxLayout();
    filters->setSpacing(8);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search: 8_19, 11n34, trefoil, Conway..."));
    search_->setClearButtonEnabled(true);
    search_->setMinimumHeight(34);

    crossingFilter_ = new QComboBox(this);
    crossingFilter_->setMinimumHeight(34);
    crossingFilter_->addItem(tr("All crossing numbers"), 0);

    for (int c = 3; c <= 13; ++c)
        crossingFilter_->addItem(tr("%1 crossings").arg(c), c);

    typeFilter_ = new QComboBox(this);
    typeFilter_->setMinimumHeight(34);
    typeFilter_->addItem(tr("All knots"));
    typeFilter_->addItem(tr("Alternating"));
    typeFilter_->addItem(tr("Non-alternating"));
    typeFilter_->addItem(tr("Fibered"));
    typeFilter_->addItem(tr("Amphicheiral"));

    filters->addWidget(search_, 3);
    filters->addWidget(crossingFilter_, 1);
    filters->addWidget(typeFilter_, 1);

    layout->addLayout(filters);

    /*
     * List | preview and details
     */
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    QWidget *left = new QWidget(splitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    model_ = new KnotListModel(this);

    list_ = new QListView(left);
    list_->setModel(model_);
    list_->setUniformItemSizes(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);

    countLabel_ = new QLabel(left);
    countLabel_->setObjectName(QStringLiteral("Subtitle"));

    leftLayout->addWidget(list_, 1);
    leftLayout->addWidget(countLabel_);

    QWidget *right = new QWidget(splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(10);

    preview_ = new KnotGLWidget(right);
    preview_->setMinimumSize(320, 300);
    preview_->setAutoRotation(true);

    title_ = new QLabel(right);
    title_->setObjectName(QStringLiteral("Stat"));
    title_->setWordWrap(true);

    details_ = new QLabel(right);
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::RichText);
    details_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    rightLayout->addWidget(preview_, 3);
    rightLayout->addWidget(title_);
    rightLayout->addWidget(details_, 2);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>{ 300, 760 });

    left->setMinimumWidth(240);

    layout->addWidget(splitter, 1);

    /*
     * Credit and buttons
     */
    QLabel *credit = new QLabel(
        tr("Knot data: C. Livingston and A. H. Moore, <i>KnotInfo: Table of "
           "Knot Invariants</i>, knotinfo.org."),
        this);

    credit->setObjectName(QStringLiteral("Subtitle"));

    QDialogButtonBox *buttons = new QDialogButtonBox(this);

    loadButton_ = buttons->addButton(tr("Load knot"), QDialogButtonBox::AcceptRole);
    loadButton_->setObjectName(QStringLiteral("GenerateButton"));
    loadButton_->setMinimumHeight(36);
    loadButton_->setEnabled(false);

    buttons->addButton(QDialogButtonBox::Cancel);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->addWidget(credit, 1);
    bottom->addWidget(buttons);

    layout->addLayout(bottom);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(search_, &QLineEdit::textChanged, this, [this]() { applyFilter(); });

    connect(crossingFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyFilter(); });

    connect(typeFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { applyFilter(); });

    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &index) { currentChanged(index); });

    connect(list_, &QListView::doubleClicked, this, [this](const QModelIndex &index)
    {
        currentChanged(index);

        if (selected_ >= 0)
            accept();
    });

    if (!table.isLoaded())
    {
        title_->setText(table.error());
        search_->setEnabled(false);
        return;
    }

    applyFilter();

    // start with the current knot, or the first one
    const int start = std::max(0, table.indexOf(current));
    const int row = model_->rowOf(start);

    if (row >= 0)
    {
        list_->setCurrentIndex(model_->index(row));
        list_->scrollTo(model_->index(row), QAbstractItemView::PositionAtCenter);
    }

    search_->setFocus();
}

QString KnotBrowserDialog::selectedKnot() const
{
    const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

    return selected_ >= 0 && selected_ < table.size() ? table.at(selected_).name : QString();
}

void KnotBrowserDialog::applyFilter()
{
    const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

    const QString key = searchKey(search_->text());
    const int crossings = crossingFilter_->currentData().toInt();
    const int type = typeFilter_->currentIndex();

    QVector<int> rows;
    rows.reserve(table.size());

    for (int i = 0; i < table.size(); ++i)
    {
        const KnotMath::KnotTableRecord &r = table.at(i);

        if (crossings > 0 && r.crossings != crossings)
            continue;

        if ((type == 1 && !r.alternating) ||
            (type == 2 && r.alternating) ||
            (type == 3 && !r.fibered) ||
            (type == 4 && !r.symmetry.contains(QLatin1String("amphicheiral"))))
        {
            continue;
        }

        // number ("8_19"), Conway name, common name or family ("twist")
        if (!key.isEmpty() &&
            !searchKey(r.name).startsWith(key) &&
            !searchKey(r.conwayName).startsWith(key) &&
            !searchKey(KnotMath::KnotTable::commonName(r.name)).contains(key) &&
            !searchKey(KnotMath::KnotTable::familyName(r)).contains(key))
        {
            continue;
        }

        rows.push_back(i);
    }

    const int keep = selected_;

    model_->setRows(rows);

    countLabel_->setText(tr("%1 of %2 knots").arg(rows.size()).arg(table.size()));

    const int row = model_->rowOf(keep);

    if (row >= 0)
        list_->setCurrentIndex(model_->index(row));
    else if (!rows.isEmpty())
        list_->setCurrentIndex(model_->index(0));
    else
        showKnot(-1);
}

void KnotBrowserDialog::currentChanged(const QModelIndex &index)
{
    showKnot(index.isValid() ? model_->tableIndex(index.row()) : -1);
}

void KnotBrowserDialog::showKnot(int tableIndex)
{
    const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

    selected_ = tableIndex;
    loadButton_->setEnabled(tableIndex >= 0);

    if (tableIndex < 0 || tableIndex >= table.size())
    {
        title_->setText(tr("No knot matches."));
        details_->clear();
        preview_->setMesh(KnotMath::Mesh());
        return;
    }

    const KnotMath::KnotTableRecord &r = table.at(tableIndex);

    // preview: KnotInfo's curve as a smooth tube
    const QVector<KnotMath::Vec3> curve =
        KnotMath::refineCurve(table.curve(tableIndex), 3, true);

    KnotMath::Mesh mesh = KnotMath::buildTubeMesh(curve, 0.12, 24, true);
    KnotMath::computeAmbientOcclusion(mesh, 0.7);

    preview_->setMesh(mesh);

    const QString described = KnotMath::KnotTable::descriptiveName(r);
    const QString name = KnotMath::KnotTable::displayName(r.name);

    title_->setText(described.isEmpty() ? tr("Knot %1").arg(name)
                                        : QStringLiteral("%1 · %2").arg(name, described));

    auto row = [](const QString &label, const QString &value)
    {
        return QStringLiteral("<tr><td style='color:#8A8780; padding-right:14px;'>%1</td>"
                              "<td>%2</td></tr>")
            .arg(label, value.toHtmlEscaped());
    };

    QString html = QStringLiteral("<table cellspacing='3'>");

    // the family as well when the title shows the common name
    const QString family = KnotMath::KnotTable::familyName(r);

    if (!family.isEmpty() && family != described)
        html += row(tr("Family"), family);

    html += row(tr("Crossing number"), QString::number(r.crossings));
    html += row(tr("Alternating"), r.alternating ? tr("yes") : tr("no"));
    html += row(tr("Unknotting number"), r.unknotting.isEmpty() ? tr("unknown") : r.unknotting);

    if (r.genus >= 0)
        html += row(tr("Genus"), QString::number(r.genus));

    html += row(tr("Signature"),
                QString::number(r.signature).replace(QLatin1Char('-'), QChar(0x2212)));
    html += row(tr("Determinant"), QString::number(r.determinant));
    html += row(tr("Fibered"), r.fibered ? tr("yes") : tr("no"));

    if (!r.symmetry.isEmpty())
        html += row(tr("Symmetry"), r.symmetry);

    html += row(tr("Hyperbolic volume"),
                r.volume > 0.0 ? QString::number(r.volume, 'f', 6) : tr("not hyperbolic"));

    if (r.braidIndex > 0)
        html += row(tr("Braid index"), QString::number(r.braidIndex));

    if (!r.braidNotation.isEmpty())
        html += row(tr("Braid word"), r.braidNotation);

    if (!r.conwayName.isEmpty() && r.conwayName != r.name)
        html += row(tr("Conway name"), r.conwayName);

    if (!r.conwayNotation.isEmpty())
        html += row(tr("Conway notation"), r.conwayNotation);

    html += row(tr("Alexander polynomial"), KnotMath::KnotTable::alexanderText(r));
    html += row(tr("Jones polynomial"), KnotMath::KnotTable::jonesText(r));

    html += QStringLiteral("</table>");

    details_->setText(Theme::html(html));
}
