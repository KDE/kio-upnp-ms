/*
    This file is part of the UPnP MediaServer Kioslave library, part of the KDE project.

    Copyright 2010 Nikhil Marathe <nsm.nikhil@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DBUSCODEC_H
#define DBUSCODEC_H

/*
 * Adapted from Friedrich Kossebau's Cagibi daemon.
 */

// lib
#include "deviceinfo.h"
// Qt
#include <QtCore/QMetaType>

class QDBusArgument;
QDBusArgument& operator<<( QDBusArgument& argument,
                           const DeviceInfo& device );
const QDBusArgument& operator>>( const QDBusArgument& argument,
                                 DeviceInfo& device );

Q_DECLARE_METATYPE( DeviceInfo )

#endif
