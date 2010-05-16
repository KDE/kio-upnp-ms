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

#ifndef DIDL_OBJECTS_H
#define DIDL_OBJECTS_H

#include <QUrl>

namespace DIDL {

// container and item form a nice hierarchy,
// but this is there just to get templates to work fine
// ah.. the things needed for standards compliance :-)
// oops, ok id is common
class SuperObject : public QObject
{
  Q_OBJECT
  public:
    enum Type {
        Description,
        Item,
        Container
    };

    SuperObject( Type t, const QString &id ) : m_type(t), m_id(id) {};
    Type type() const { return m_type; };
    QString id() const { return m_id; };

  private:
    Type m_type;
    QString m_id;
};

/**
 * Contains a DIDL 'desc' element.
 * Called Description because the element is a 'descriptor',
 * but it contains a description.
 * A description doesn't care about internal elements and so on.
 * It stores everything inside as text.
 */
class Description : public SuperObject
{
  Q_OBJECT
  public:
    /**
     * Create a new Description with the id and the namespace
     */
    Description( const QString &id, const QUrl &ns );

    QString description() const { return m_description; };
    void setDescription( const QString &desc ) { m_description = desc; };


    QUrl nameSpace() const { return m_namespace; };

    QString type() const { return m_type; };
    void setType( const QString &type ) { m_type = type; };


  private:
    QString m_description;
    QString m_type;
    QUrl m_namespace;
};

class Object : public SuperObject
{
  Q_OBJECT
  public:
    Object( Type type, const QString &id, const QString &parentId, bool restricted );

    QString parentId() const { return m_parentId; };
    bool restricted() const { return m_restricted; };

    // since dc:title and upnp:class are REQUIRED
    // but child elements, they aren't in the constructor
    QString title() const { return m_title; };
    QString upnpClass() const { return m_upnpClass; };

    void setTitle( const QString &title ) { m_title = title; };
    void setUpnpClass( const QString &upnpClass ) { m_upnpClass = upnpClass; };

  private:
    QString m_parentId;
    bool m_restricted;
    QString m_title;
    QString m_upnpClass;
};

class Container : public Object
{
  Q_OBJECT
  public:
    Container( const QString &id, const QString &parentId, bool restricted );

};

class Item : public Object
{
  Q_OBJECT
  public:
    Item( const QString &id, const QString &parentId, bool restricted );

};
} //~ namespace

#endif
