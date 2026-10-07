#pragma once
#include "tool/ToolPage.h"
#include "CharacterArt.h"
#include <QFutureWatcher>
#include <functional>
class QLabel;
class ElaPushButton;
class ElaSpinBox;
class ElaDoubleSpinBox;
class ElaComboBox;
class QCheckBox;
class ArtCanvas;
class CharacterArtPage : public ToolPage {
    Q_OBJECT
public:
    explicit CharacterArtPage(QWidget* parent = nullptr);
    ~CharacterArtPage() override;
    void onDeactivated() override;
    void openImage(const QString& path);
    const characterart::Result& result() const { return m_result; }
    bool saveText(const QString& path);
    bool savePng(const QString& path);
    void setClipboardWriter(std::function<bool(const QString&)> writer) { m_copyWriter = std::move(writer); }
protected:
    void changeEvent(QEvent* event) override;
private:
    struct Work { characterart::LoadedImage loaded; characterart::Result result; bool loading = false; };
    void invalidate();
    void generate();
    void refresh();
    void retranslate();
    characterart::Maple m_maple;
    QImage m_source, m_rendered;
    characterart::Result m_result;
    QFutureWatcher<Work> m_worker;
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_revision = 0, m_submitted = 0;
    QString m_path, m_message;
    std::function<bool(const QString&)> m_copyWriter;
    QLabel *m_title, *m_hint, *m_info, *m_sourcePreview, *m_status;
    QLabel *m_columnsLabel, *m_paletteLabel, *m_brightnessLabel, *m_contrastLabel, *m_zoomLabel;
    ElaPushButton *m_open, *m_generate, *m_stop, *m_clear, *m_copy, *m_txt, *m_png;
    ElaSpinBox *m_columns, *m_zoom;
    ElaDoubleSpinBox *m_brightness, *m_contrast;
    ElaComboBox* m_palette;
    QCheckBox* m_invert;
    ArtCanvas* m_canvas;
};
