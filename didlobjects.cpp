/********************************************************************
 This file is part of the KDE project.

Copyright (C) 2010 Nikhil Marathe <nsm.nikhil@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "didlobjects.h"

namespace DIDL {

Description::Description( const QString &id, const QUrl &ns )
    : SuperObject( SuperObject::Description, id )
    , m_namespace( ns )
{
}

Object::Object( Type type, const QString &id, const QString &parentId, bool restricted )
    : SuperObject( type, id )
    , m_parentId( parentId )
    , m_restricted( restricted )
{
}

Container::Container( const QString &id, const QString &parentId, bool restricted )
    : Object( SuperObject::Container, id, parentId, restricted )
{
}

Item::Item( const QString &id, const QString &parentId, bool restricted )
    : Object( SuperObject::Item, id, parentId, restricted )
{
}
} //~ namespace
