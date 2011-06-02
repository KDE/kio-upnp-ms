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

#include <QStringList>
#include <QXmlStreamReader>
#include <klocale.h>

#include <kdebug.h>

#include "didlobjects.h"

namespace DIDL {
Parser::Parser()
    : QObject( 0 )
    , m_reader( 0 )
{
}

Parser::~Parser()
{
    delete m_reader;
}

bool Parser::interpretRestricted(const QStringRef &res)
{
    if( res == QLatin1String("1") )
        return true;
    return false;
}

void Parser::raiseError( const QString &errorStr )
{
    m_reader->raiseError( errorStr );
    emit error( errorStr );
    m_reader->clear();
}

Resource Parser::parseResource()
{
    Resource r;
    QString protocolInfo = m_reader->attributes().value(QLatin1String("protocolInfo")).toString();
    if( !protocolInfo.isEmpty() ) {
        QStringList fields = protocolInfo.split(QLatin1String(":"));
        if( fields.length() != 4 ) {
            raiseError( i18n("Bad protocolInfo %1", (protocolInfo)) );
            return Resource();
        }
        r.insert(QLatin1String("mimetype"), fields[2]);
    }

    foreach( QXmlStreamAttribute attr, m_reader->attributes() ) {
        r.insert(attr.name().toString(), attr.value().toString());
    }
    r.insert(QLatin1String("uri"), m_reader->readElementText());

    return r;
}

bool Parser::parseObjectCommon( Object *o )
{
    if( m_reader->name() == QLatin1String("title") ) {
        o->setTitle( QString( m_reader->readElementText().replace( QLatin1String("/"), QLatin1String("%2f") ) ) );
        return true;
    }
    else if( m_reader->name() == QLatin1String("class") ) {
        o->setUpnpClass( m_reader->readElementText() );
        return true;
    }
    return false;
}

void Parser::parseItem()
{
    QXmlStreamAttributes attributes = m_reader->attributes();
    Item *item = new Item(
        attributes.value(QLatin1String("id")).toString(),
        attributes.value(QLatin1String("parentID")).toString(),
        interpretRestricted( attributes.value(QLatin1String("restricted")) ) );
    if( attributes.hasAttribute(QLatin1String("refID")) )
        item->setRefId( attributes.value(QLatin1String("refID")).toString() );

    while( m_reader->readNextStartElement() ) {
        if( parseObjectCommon( item ) ) {
        }
        else if( m_reader->name() == QLatin1String("res") ) {
            item->addResource( parseResource() );
        }
        else {
            item->setDataItem( m_reader->name().toString(), m_reader->readElementText() );
        }
    }

    emit itemParsed( item );
}

void Parser::parseContainer()
{
    QXmlStreamAttributes attributes = m_reader->attributes();
    Container *container = new Container(
        attributes.value(QLatin1String("id")).toString(),
        attributes.value(QLatin1String("parentID")).toString(),
        interpretRestricted( attributes.value(QLatin1String("restricted")) ) );

    if( attributes.hasAttribute(QLatin1String("childCount")) )
        container->setDataItem( QLatin1String("childCount"),
                                attributes.value(QLatin1String("childCount")).toString() );

    while( m_reader->readNextStartElement() ) {
        if( parseObjectCommon( container ) ) {
        }
        else {
            container->setDataItem( m_reader->name().toString(), m_reader->readElementText() );
        }
    }

    emit containerParsed( container );
}

void Parser::parseDescription()
{
    QXmlStreamAttributes attributes = m_reader->attributes();
    Description *description = new Description(
        attributes.value(QLatin1String("id")).toString(),
        attributes.value(QLatin1String("nameSpace")).toString() );
    description->setDescription( m_reader->readElementText() );
}

void Parser::parse(const QString &input)
{
    if( m_reader ) {
        delete m_reader;
    }
    m_reader = new QXmlStreamReader(input);
    while( !m_reader->atEnd() ) {
        if( !m_reader->readNextStartElement() )
            break;
        if( m_reader->name() == QLatin1String("item") ) {
            parseItem();
        }
        else if( m_reader->name() == QLatin1String("container") ) {
            parseContainer();
        }
        else if( m_reader->name() == QLatin1String("description") ) {
            parseDescription();
        }
        else if( m_reader->name() == QLatin1String("DIDL-Lite") ) {
        }
        else {
            raiseError(QLatin1String("Unexpected element") + m_reader->name().toString() );
        }
    }

    if( m_reader->hasError() )
        raiseError(m_reader->errorString());
    else
        emit done();
}

} //~ namespace

