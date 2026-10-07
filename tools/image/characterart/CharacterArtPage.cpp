#include "CharacterArtPage.h"
#include "UiTheme.h"
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaDoubleSpinBox.h>
#include <ElaComboBox.h>
#include <ElaScrollArea.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPainter>
#include <QBuffer>
#include <QEvent>
#include <QtConcurrentRun>

class ArtCanvas : public QWidget {
public:
    using QWidget::QWidget;
    void setImage(const QImage& image) { m_image = image; updateSize(); }
    void setZoom(int percent) { m_zoom = percent / 100.0; updateSize(); }
protected:
    void paintEvent(QPaintEvent*) override { QPainter painter(this); painter.fillRect(rect(), Qt::white); painter.scale(m_zoom,m_zoom); painter.drawImage(0,0,m_image); }
private:
    void updateSize() { setFixedSize(m_image.isNull() ? QSize(1,1) : QSize(qRound(m_image.width()*m_zoom),qRound(m_image.height()*m_zoom))); update(); }
    QImage m_image; double m_zoom = 1;
};
using namespace characterart;
CharacterArtPage::CharacterArtPage(QWidget* parent) : ToolPage(parent), m_maple(loadMaple()) {
    setObjectName("characterArtPage");
    m_copyWriter=[](const QString& text) { auto* clipboard=QApplication::clipboard(); clipboard->setText(text); return clipboard->ownsClipboard(); };
    auto* outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0);
    auto* scroll=new ElaScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto* content=new QWidget(scroll); scroll->setWidget(content);
    auto backdrop=[this,content,scroll] { auto palette=qApp->palette(); const auto color=eTheme->getThemeColor(eTheme->getThemeMode(),ElaThemeType::WindowBase);
        palette.setColor(QPalette::Window,color); for(auto* w:{static_cast<QWidget*>(this),content,scroll->viewport()}) { w->setPalette(palette); w->setAutoFillBackground(true); } };
    connect(eTheme,&ElaTheme::themeModeChanged,this,backdrop); backdrop();
    auto* layout=new QVBoxLayout(content); layout->setContentsMargins(32,28,32,28); layout->setSpacing(12); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_title=ui::label({},18,ui::Tone::Primary,content); m_hint=ui::label({},10,ui::Tone::Secondary,content); m_hint->setWordWrap(true);
    m_info=ui::label({},10,ui::Tone::Secondary,content); m_info->setWordWrap(true); m_info->setTextFormat(Qt::PlainText);
    m_status=ui::label({},10,ui::Tone::Secondary,content); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); m_status->setObjectName("artStatus");
    const auto button=[content](const char* name) { auto* w=new ElaPushButton(content); w->setObjectName(name); return w; };
    m_open=button("artOpen"); m_generate=button("artGenerate"); ui::primaryButton(m_generate); m_stop=button("artCancel"); m_clear=button("artClear");
    m_copy=button("artCopy"); m_txt=button("artSaveText"); m_png=button("artSavePng");
    auto* actions=new QHBoxLayout; for(auto* b:{m_open,m_generate,m_stop,m_clear}) actions->addWidget(b); actions->addStretch();
    m_columns=new ElaSpinBox(content); m_columns->setObjectName("artColumns"); m_columns->setRange(10,MaxColumns); m_columns->setValue(80);
    m_palette=new ElaComboBox(content); m_palette->setObjectName("artPalette"); for(const auto& palette:m_maple.palettes) m_palette->addItem({},palette.id);
    m_brightness=new ElaDoubleSpinBox(content); m_brightness->setObjectName("artBrightness"); m_brightness->setRange(-1,1); m_brightness->setSingleStep(.1);
    m_contrast=new ElaDoubleSpinBox(content); m_contrast->setObjectName("artContrast"); m_contrast->setRange(0,3); m_contrast->setSingleStep(.1); m_contrast->setValue(1);
    m_zoom=new ElaSpinBox(content); m_zoom->setObjectName("artZoom"); m_zoom->setRange(25,200); m_zoom->setSingleStep(25); m_zoom->setValue(100); m_zoom->setSuffix("%");
    m_invert=new QCheckBox(content); m_invert->setObjectName("artInvert");
    auto label=[content] { return ui::label({},10,ui::Tone::Secondary,content); };
    m_columnsLabel=label(); m_paletteLabel=label(); m_brightnessLabel=label(); m_contrastLabel=label(); m_zoomLabel=label();
    auto* form=new QFormLayout; form->addRow(m_columnsLabel,m_columns); form->addRow(m_paletteLabel,m_palette);
    form->addRow(m_brightnessLabel,m_brightness); form->addRow(m_contrastLabel,m_contrast); form->addRow(m_zoomLabel,m_zoom); form->addRow(m_invert);
    m_sourcePreview=new QLabel(content); m_sourcePreview->setObjectName("artSourcePreview"); m_sourcePreview->setAlignment(Qt::AlignCenter); m_sourcePreview->setFixedSize(160,160);
    auto* options=new QHBoxLayout; options->addLayout(form,1); options->addWidget(m_sourcePreview);
    auto* preview=new ElaScrollArea(content); preview->setObjectName("artPreview"); preview->setMinimumHeight(240); preview->setWidgetResizable(false);
    m_canvas=new ArtCanvas(preview); preview->setWidget(m_canvas); m_canvas->setImage({});
    auto* exports=new QHBoxLayout; for(auto* b:{m_copy,m_txt,m_png}) exports->addWidget(b); exports->addStretch();
    layout->addWidget(m_title); layout->addWidget(m_hint); layout->addLayout(actions); layout->addWidget(m_info); layout->addLayout(options); layout->addWidget(preview); layout->addLayout(exports); layout->addWidget(m_status); layout->addStretch();
    connect(m_open,&ElaPushButton::clicked,this,[this] { const auto path=QFileDialog::getOpenFileName(this,tr("Open image"),{},tr("Images (*.png *.jpg *.jpeg)")); if(!path.isEmpty()) openImage(path); });
    connect(m_generate,&ElaPushButton::clicked,this,&CharacterArtPage::generate);
    connect(m_stop,&ElaPushButton::clicked,this,[this] { invalidate(); m_message=tr("Generation cancelled."); refresh(); });
    connect(m_clear,&ElaPushButton::clicked,this,[this] { invalidate(); m_source={}; m_path.clear(); m_sourcePreview->clear(); refresh(); });
    connect(m_columns,qOverload<int>(&QSpinBox::valueChanged),this,&CharacterArtPage::invalidate);
    connect(m_palette,&QComboBox::currentIndexChanged,this,&CharacterArtPage::invalidate);
    connect(m_brightness,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&CharacterArtPage::invalidate);
    connect(m_contrast,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&CharacterArtPage::invalidate);
    connect(m_invert,&QCheckBox::toggled,this,&CharacterArtPage::invalidate);
    connect(m_zoom,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value) { m_canvas->setZoom(value); });
    connect(m_copy,&ElaPushButton::clicked,this,[this] { if(m_result.success()) { m_message=m_copyWriter(m_result.text) ? tr("Text copied.") : tr("Could not obtain clipboard ownership. Retry copying."); refresh(); } });
    connect(m_txt,&ElaPushButton::clicked,this,[this] { const auto path=QFileDialog::getSaveFileName(this,tr("Save TXT"),"character-art.txt",tr("Text files (*.txt)"),nullptr,QFileDialog::DontConfirmOverwrite); if(!path.isEmpty()) saveText(path); });
    connect(m_png,&ElaPushButton::clicked,this,[this] { const auto path=QFileDialog::getSaveFileName(this,tr("Export PNG"),"character-art.png",tr("PNG images (*.png)"),nullptr,QFileDialog::DontConfirmOverwrite); if(!path.isEmpty()) savePng(path); });
    connect(&m_worker,&QFutureWatcher<Work>::finished,this,[this] {
        if(m_submitted!=m_revision) { refresh(); return; }
        const auto work=m_worker.result();
        if(work.loading) {
            m_source=work.loaded.image; m_message=work.loaded.error;
            if(!m_source.isNull()) m_sourcePreview->setPixmap(QPixmap::fromImage(m_source.scaled(160,160,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
        } else {
            m_result=work.result; m_message=m_result.error;
            if(m_result.success()) { m_rendered=characterart::render(m_result,m_maple); if(m_rendered.isNull()) { m_result={}; m_message=tr("Could not render the character art."); } m_canvas->setImage(m_rendered); }
        }
        refresh();
    });
    retranslate(); refresh();
}
CharacterArtPage::~CharacterArtPage() { if(m_cancel) m_cancel->store(true); }
void CharacterArtPage::onDeactivated() { if(m_worker.isRunning()) invalidate(); }
void CharacterArtPage::invalidate() {
    ++m_revision; if(m_cancel) m_cancel->store(true); m_result={}; m_rendered={}; m_canvas->setImage({}); m_message.clear(); refresh();
}
void CharacterArtPage::openImage(const QString& path) {
    if(m_worker.isRunning() || !m_maple.valid()) return;
    invalidate(); m_source={}; m_sourcePreview->clear(); m_path=path; m_submitted=m_revision;
    m_cancel=std::make_shared<std::atomic_bool>(false);
    m_worker.setFuture(QtConcurrent::run([path] { Work work; work.loading=true; work.loaded=loadImage(path); return work; })); refresh();
}
void CharacterArtPage::generate() {
    if(m_source.isNull() || m_worker.isRunning() || !m_maple.valid()) return;
    invalidate(); m_submitted=m_revision; m_cancel=std::make_shared<std::atomic_bool>(false);
    const Options options{m_columns->value(),m_brightness->value(),m_contrast->value(),m_invert->isChecked()};
    const auto source=m_source; const auto palette=m_maple.palettes[m_palette->currentIndex()]; const auto cell=m_maple.cell; const auto cancel=m_cancel;
    m_worker.setFuture(QtConcurrent::run([source,options,palette,cell,cancel] { Work work; work.result=characterart::generate(source,options,palette,cell,cancel); return work; })); refresh();
}
bool CharacterArtPage::saveText(const QString& path) {
    if(!m_result.success()) return false;
    QString error; const bool ok=saveNewFile(path,m_result.text.toUtf8(),error); m_message=ok ? tr("Saved a new file.") : error; refresh(); return ok;
}
bool CharacterArtPage::savePng(const QString& path) {
    if(!m_result.success() || m_rendered.isNull()) return false;
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if(!m_rendered.save(&buffer,"PNG")) { m_message=tr("Could not encode PNG."); refresh(); return false; }
    QString error; const bool ok=saveNewFile(path,bytes,error); m_message=ok ? tr("Saved a new file.") : error; refresh(); return ok;
}
void CharacterArtPage::refresh() {
    const bool busy=m_worker.isRunning(); m_open->setEnabled(!busy && m_maple.valid()); m_generate->setEnabled(!busy && !m_source.isNull() && m_maple.valid()); m_stop->setEnabled(busy);
    // Parameter edits must not invalidate a pending image load and lose the selection.
    const bool loading=busy && m_source.isNull();
    for(auto* field:{static_cast<QWidget*>(m_columns),static_cast<QWidget*>(m_palette),static_cast<QWidget*>(m_brightness),static_cast<QWidget*>(m_contrast),static_cast<QWidget*>(m_invert)}) field->setEnabled(!loading);
    for(auto* b:{m_copy,m_txt,m_png}) b->setEnabled(!busy && m_result.success());
    m_clear->setEnabled(busy || !m_path.isEmpty());
    m_info->setText(m_source.isNull() ? tr("Select a PNG or JPEG image. Nothing is uploaded.") : tr("%1 · %2 × %3 pixels · Maple cell %4 × %5").arg(QFileInfo(m_path).fileName()).arg(m_source.width()).arg(m_source.height()).arg(m_maple.cell.width()).arg(m_maple.cell.height()));
    QString message;
    if(!m_maple.valid()) message=m_maple.error;
    else if(busy) message=tr("Working… You can cancel or clear.");
    else if(!m_message.isEmpty()) message=m_message;
    else if(m_result.success()) message=tr("Generated %1 columns × %2 rows. TXT uses UTF-8; PNG preserves the Maple layout.").arg(m_result.columns).arg(m_result.rows);
    else message=tr("Choose an image and click Generate.");
    m_status->setText(QCoreApplication::translate("CharacterArtErrors",message.toUtf8().constData()));
}
void CharacterArtPage::retranslate() {
    m_title->setText(tr("Character Art Generator")); m_hint->setText(tr("Local image-to-text conversion using Maple Mono NL NFMono CN. Copied text may look different in another font; PNG keeps the exact preview layout."));
    m_open->setText(tr("Open image")); m_generate->setText(tr("Generate")); m_stop->setText(tr("Cancel")); m_clear->setText(tr("Clear")); m_copy->setText(tr("Copy text")); m_txt->setText(tr("Save TXT")); m_png->setText(tr("Export PNG"));
    m_columnsLabel->setText(tr("Columns")); m_paletteLabel->setText(tr("Character set")); m_brightnessLabel->setText(tr("Brightness")); m_contrastLabel->setText(tr("Contrast")); m_zoomLabel->setText(tr("Preview zoom")); m_invert->setText(tr("Invert"));
    if(m_palette->count()>0) m_palette->setItemText(0,tr("Basic")); if(m_palette->count()>1) m_palette->setItemText(1,tr("Dense"));
}
void CharacterArtPage::changeEvent(QEvent* event) { ToolPage::changeEvent(event); if(event->type()==QEvent::LanguageChange) { retranslate(); refresh(); } }
