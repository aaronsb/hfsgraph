// Single source of truth for how a file type is presented (ADR-301): its theme
// icon and its colour, both keyed by filename extension. The treemap's icon LOD
// rung draws the icon; the finer pixel-dot rung draws the colour; a future coloured
// filename reads the same colour — so a type's whole look is changed in one place,
// and the dot, the icon, and the name always agree.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace ui {

// Theme icon for a filename (by extension, name-only — no disk stat), cached. The
// same icons a file manager shows.
QIcon fileTypeIcon(const QString &name);

// Representative colour for a filename's type, cached. Curated choices for common
// categories at compile time; anything unlisted gets a deterministic colour
// generated at runtime (a stable hue hashed from the mime type), so every type ends
// up with a stable, distinct colour.
QColor fileTypeColor(const QString &name);

} // namespace ui
