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

#include "didlparser.h"
#include "didlobjects.h"

#include <QXmlStreamReader>

#include <kdebug.h>

namespace DIDL {
Parser::Parser()
    : QObject(NULL)
    , m_reader(NULL)
{
}

Parser::~Parser()
{
}

void Parser::parse(const QString &input)
{
    // minimal parsing just to test the resolver
    if( m_reader ) {
        // TODO should probably just create this on the stack
        delete m_reader;
    }
    m_reader = new QXmlStreamReader(input);
    while( !m_reader->atEnd() ) {
        if( !m_reader->readNextStartElement() )
            break;
        QXmlStreamAttributes attributes = m_reader->attributes();
        Object *object = NULL;
        if( m_reader->name() == "item" ) {
            object = new Item( attributes.value("id").toString(),
                               attributes.value("parentID").toString(),
                // TODO boolean conversion
                               false );
        }
        else if( m_reader->name() == "container" ) {
            object = new Container( attributes.value("id").toString(),
                               attributes.value("parentID").toString(),
                // TODO boolean conversion
                               false );
        }

        if( object ) {
            // process title
            m_reader->readNextStartElement();
            if( m_reader->name() == "title" ) {
                object->setTitle( m_reader->readElementText() );
            }

            if( object->type() == SuperObject::Item )
                emit itemParsed( static_cast<Item*>( object ) );
            else
                emit containerParsed( static_cast<Container*>( object ) );
            // skip remaining attributes for now
            m_reader->skipCurrentElement();

        }
    }

    if( m_reader->hasError() )
        emit error(m_reader->errorString());
    else
        emit done();
}

} //~ namespace

