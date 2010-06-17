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
}

bool Parser::interpretRestricted(const QStringRef &res)
{
    if( res == "1" )
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
    QString protocolInfo = m_reader->attributes().value("protocolInfo").toString();
    if( !protocolInfo.isEmpty() ) {
        QStringList fields = protocolInfo.split(":");
        if( fields.length() != 4 ) {
            raiseError( i18n("Bad protocolInfo %1", (protocolInfo)) );
            return Resource();
        }
        r["mimetype"] = fields[2];
    }

    foreach( QXmlStreamAttribute attr, m_reader->attributes() ) {
        r[attr.name().toString()] = attr.value().toString();
    }
    r["uri"] = m_reader->readElementText();

    return r;
}

bool Parser::parseObjectCommon( Object *o )
{
    if( m_reader->name() == "title" ) {
        o->setTitle( QString( QUrl::toPercentEncoding( m_reader->readElementText() ) ) );
        return true;
    }
    else if( m_reader->name() == "class" ) {
        o->setUpnpClass( m_reader->readElementText() );
        return true;
    }
    return false;
}

void Parser::parseItem()
{
    QXmlStreamAttributes attributes = m_reader->attributes();
    Item *item = new Item(
        attributes.value("id").toString(),
        attributes.value("parentID").toString(),
        interpretRestricted( attributes.value("restricted") ) );

    while( m_reader->readNextStartElement() ) {
        if( parseObjectCommon( item ) ) {
        }
        else if( m_reader->name() == "res" ) {
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
        attributes.value("id").toString(),
        attributes.value("parentID").toString(),
        interpretRestricted( attributes.value("restricted") ) );

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
        attributes.value("id").toString(),
        attributes.value("nameSpace").toString() );
    description->setDescription( m_reader->readElementText() );
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
        if( m_reader->name() == "item" ) {
            parseItem();
        }
        else if( m_reader->name() == "container" ) {
            parseContainer();
        }
        else if( m_reader->name() == "description" ) {
            parseDescription();
        }
        else if( m_reader->name() == "DIDL-Lite" ) {
        }
        else {
            raiseError("Unexpected element" + m_reader->name().toString() );
        }
    }

    if( m_reader->hasError() )
        raiseError(m_reader->errorString());
    else
        emit done();
}

} //~ namespace

