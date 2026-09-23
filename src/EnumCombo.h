#ifndef ENUMCOMBO_H
#define ENUMCOMBO_H

#include "DeviceConfig.h"
#include <QComboBox>

// Combo boxes for protobuf enums. Each item stores the enum number as its
// data, so reading back uses currentData(), never currentIndex(): enum values
// are not list positions once deprecated values are hidden.

// Adds the non-deprecated options. With showName, items read
// "EU_868 - European Union 868MHz" rather than just the label.
inline void populateEnumCombo(QComboBox *combo, const QList<DeviceConfig::EnumOption> &options,
                              bool showName = false)
{
    combo->clear();
    for (const auto &o : options) {
        if (o.deprecated)
            continue;
        const QString text = (showName && o.label != o.name)
                                 ? QString("%1 - %2").arg(o.name, o.label)
                                 : o.label;
        combo->addItem(text, o.value);
    }
}

// Selects `value`. A value the combo does not list (deprecated, or newer than
// our protos) is added rather than silently leaving another item selected -
// otherwise the next save would write that other value to the device.
inline void selectEnumValue(QComboBox *combo, const QList<DeviceConfig::EnumOption> &options, int value)
{
    int idx = combo->findData(value);
    if (idx < 0) {
        QString text = QString("Unknown (%1)").arg(value);
        for (const auto &o : options)
            if (o.value == value)
                text = o.label + (o.deprecated ? " (deprecated)" : "");
        combo->addItem(text, value);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx);
}

inline int enumComboValue(const QComboBox *combo)
{
    return combo->currentData().toInt();
}

#endif // ENUMCOMBO_H
