/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010 Facebook, Inc. (http://www.facebook.com)          |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include <runtime/base/variable_serializer.h>
#include <runtime/base/execution_context.h>
#include <runtime/base/complex_types.h>
#include <util/exception.h>
#include <runtime/base/zend/zend_printf.h>
#include <runtime/base/zend/zend_functions.h>
#include <runtime/base/zend/zend_string.h>
#include <runtime/base/class_info.h>
#include <math.h>
#include <runtime/base/runtime_option.h>

using namespace std;

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////

VariableSerializer::VariableSerializer(Type type, int option /* = 0 */)
  : m_type(type), m_option(option), m_buf(NULL), m_indent(0),
    m_valueCount(0), m_referenced(false), m_refCount(1), m_maxCount(3),
    m_outputLimit(0) {
}

void VariableSerializer::setObjectInfo(const char *objClass, int objId) {
  m_objClass = objClass;
  m_objId = objId;
}

void VariableSerializer::getResourceInfo(std::string &rsrcName, int &rsrcId) {
  rsrcName = m_rsrcName;
  rsrcId = m_rsrcId;
}

void VariableSerializer::setResourceInfo(const char *rsrcName, int rsrcId) {
  m_rsrcName = rsrcName;
  m_rsrcId = rsrcId;
}

Variant VariableSerializer::serialize(CVarRef v, bool ret) {
  StringBuffer buf;
  m_buf = &buf;
  if (ret) {
    m_outputLimit = RuntimeOption::SerializationSizeLimit;
  }
  m_valueCount = 1;
  if (m_type == VarDump && v.isContagious()) m_buf->append('&');
  write(v);
  if (ret) {
    return m_buf->detach();
  } else {
    String str = m_buf->detach();
    g_context->write(str.c_str(), str.size());
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////

void VariableSerializer::write(bool v) {
  switch (m_type) {
  case PrintR:
    if (v) m_buf->append(1);
    break;
  case VarExport:
  case JSON:
    m_buf->append(v ? "true" : "false");
    break;
  case VarDump:
  case DebugDump:
    indent();
    m_buf->append(v ? "bool(true)" : "bool(false)");
    writeRefCount();
    m_buf->append('\n');
    break;
  case Serialize:
    m_buf->append(v ? "b:1;" : "b:0;");
    break;
  default:
    ASSERT(false);
    break;
  }
  checkOutputSize();
}

void VariableSerializer::write(int64 v) {
  switch (m_type) {
  case PrintR:
  case VarExport:
  case JSON:
    m_buf->append(v);
    break;
  case VarDump:
    indent();
    m_buf->append("int(");
    m_buf->append(v);
    m_buf->append(")\n");
    break;
  case DebugDump:
    indent();
    m_buf->append("long(");
    m_buf->append(v);
    m_buf->append(')');
    writeRefCount();
    m_buf->append('\n');
    break;
  case Serialize:
    m_buf->append("i:");
    m_buf->append(v);
    m_buf->append(';');
    break;
  default:
    ASSERT(false);
    break;
  }
  checkOutputSize();
}

void VariableSerializer::write(double v) {
  switch (m_type) {
  case JSON:
    if (!isinf(v) && !isnan(v)) {
      char *buf;
      if (v == 0.0) v = 0.0; // so to avoid "-0" output
      vspprintf(&buf, 0, "%.*k", 14, v);
      m_buf->append(buf);
      free(buf);
    } else {
      // PHP issues a warning: double INF/NAN does not conform to the
      // JSON spec, encoded as 0.
      m_buf->append('0');
    }
    break;
  case VarExport:
  case PrintR:
    {
      char *buf;
      if (v == 0.0) v = 0.0; // so to avoid "-0" output
      vspprintf(&buf, 0, "%.*G", 14, v);
      m_buf->append(buf);
      free(buf);
    }
    break;
  case VarDump:
  case DebugDump:
    {
      char *buf;
      if (v == 0.0) v = 0.0; // so to avoid "-0" output
      vspprintf(&buf, 0, "float(%.*G)", 14, v);
      indent();
      m_buf->append(buf);
      free(buf);
      writeRefCount();
      m_buf->append('\n');
    }
    break;
  case Serialize:
    m_buf->append("d:");
    if (isnan(v)) {
      m_buf->append("NAN");
    } else if (isinf(v)) {
      m_buf->append("INF");
    } else {
      char *buf;
      if (v == 0.0) v = 0.0; // so to avoid "-0" output
      vspprintf(&buf, 0, "%.*G", 14, v);
      m_buf->append(buf);
      free(buf);
    }
    m_buf->append(';');
    break;
  default:
    ASSERT(false);
    break;
  }
  checkOutputSize();
}

void VariableSerializer::write(const char *v, int len /* = -1 */,
                               bool isArrayKey /* = false */) {
  switch (m_type) {
  case PrintR: {
    if (len < 0) len = strlen(v);
    m_buf->append(v, len);
    break;
  }
  case VarExport: {
    if (len < 0) len = strlen(v);
    m_buf->append('\'');
    const char *p = v;
    for (int i = 0; i < len; i++, p++) {
      const char c = *p;
      // adapted from Zend php_var_export and php_addcslashes
      if (c == '\'' || c == '\\' || (!isArrayKey && c == '\0')) {
        if ((unsigned char) c < 32 || (unsigned char) c > 126) {
          m_buf->append('\\');
          char buffer[4];
          sprintf(buffer, "%03o", (unsigned char)c);
          m_buf->append(buffer);
          continue;
        } else {
          m_buf->append('\\');
        }
      }
      m_buf->append(c);
    }
    m_buf->append('\'');
    break;
  }
  case VarDump:
  case DebugDump: {
    if (v == NULL) v = "";
    if (len < 0) len = strlen(v);
    indent();
    m_buf->append("string(");
    m_buf->append(len);
    m_buf->append(") \"");
    m_buf->append(v, len);
    m_buf->append('"');
    writeRefCount();
    m_buf->append('\n');
    break;
  }
  case Serialize:
    if (len < 0) {
      len = strlen(v);
      m_buf->append("s:");
      m_buf->append(len);
      m_buf->append(":\"");
      m_buf->append(v);
      m_buf->append("\";");
    } else {
      m_buf->append("s:");
      m_buf->append(len);
      m_buf->append(":\"");
      m_buf->append(v, len);
      m_buf->append("\";");
    }
    break;
  case JSON:
    {
      if (len < 0) len = strlen(v);
      char *escaped = string_json_escape(v, len, m_option);
      m_buf->append(escaped);
      free(escaped);
    }
    break;
  default:
    ASSERT(false);
    break;
  }
  checkOutputSize();
}

void VariableSerializer::write(CStrRef v) {
  v.serialize(this);
}

void VariableSerializer::write(CArrRef v) {
  v.serialize(this);
}

void VariableSerializer::write(CObjRef v) {
  if (!v.isNull() && m_type == JSON) {
    Array props = v->o_toArray();
    ClassInfo::PropertyVec properties;
    ClassInfo::GetClassProperties(properties, v->o_getClassName());
    for (ClassInfo::PropertyVec::const_iterator iter = properties.begin();
         iter != properties.end(); ++iter) {
      if ((*iter)->attribute & ClassInfo::IsProtected) {
        props.remove((*iter)->name);
      }
    }
    // Remove private props
    for (ArrayIter it(props); !it.end(); it.next()) {
      if (it.first().toString().charAt(0) == '\0') {
        props.remove(it.first());
      }
    }
    setObjectInfo(v->o_getClassName(), v->o_getId());
    props.serialize(this);
  } else {
    v.serialize(this);
  }
}

void VariableSerializer::write(CVarRef v, bool isArrayKey /* = false */) {
  if (!isArrayKey && v.isObject()) {
    write(v.toObject());
    return;
  }
  setReferenced(v.isReferenced());
  setRefCount(v.getRefCount());
  v.serialize(this, isArrayKey);
}

void VariableSerializer::writeNull() {
  switch (m_type) {
  case PrintR:
    // do nothing
    break;
  case VarExport:
    m_buf->append("NULL");
    break;
  case VarDump:
  case DebugDump:
    indent();
    m_buf->append("NULL");
    writeRefCount();
    m_buf->append('\n');
    break;
  case Serialize:
    m_buf->append("N;");
    break;
  case JSON:
    m_buf->append("null");
    break;
  default:
    ASSERT(false);
    break;
  }
}

void VariableSerializer::writeOverflow(void* ptr, bool isObject /* = false */) {
  bool wasRef = m_referenced;
  setReferenced(false);
  switch (m_type) {
  case PrintR:
    if (!m_objClass.empty()) {
      m_buf->append(m_objClass);
      m_buf->append(" Object\n");
    } else {
      m_buf->append("Array\n");
    }
    m_buf->append(" *RECURSION*");
    break;
  case VarExport:
    throw NestingLevelTooDeepException();
  case VarDump:
  case DebugDump:
    indent();
    m_buf->append("*RECURSION*\n");
    break;
  case Serialize:
    {
      PointerCounterMap::const_iterator iter = m_arrayIds.find(ptr);
      ASSERT(iter != m_arrayIds.end());
      int id = iter->second;
      if (isObject) {
        m_buf->append("r:");
        m_buf->append(id);
        m_buf->append(';');
      } else if (wasRef) {
        m_buf->append("R:");
        m_buf->append(id);
        m_buf->append(';');
      } else {
        m_buf->append("N;");
      }
    }
    break;
  case JSON:
    m_buf->append("null");
    break;
  default:
    ASSERT(false);
    break;
  }
}

void VariableSerializer::writeRefCount() {
  if (m_type == DebugDump) {
    m_buf->append(" refcount(");
    m_buf->append(m_refCount);
    m_buf->append(')');
    m_refCount = 1;
  }
}

void VariableSerializer::writeArrayHeader(const ArrayData *arr, int size) {
  m_arrayInfos.resize(m_arrayInfos.size() + 1);
  ArrayInfo &info = m_arrayInfos.back();
  info.first_element = true;
  info.is_vector = m_objClass.empty() && arr->isVectorData();
  info.indent_delta = 0;

  switch (m_type) {
  case PrintR:
    if (!m_objClass.empty()) {
      m_buf->append(m_objClass);
      m_buf->append(" Object\n");
    } else {
      m_buf->append("Array\n");
    }
    if (m_indent > 0) {
      m_indent += 4;
      indent();
    }
    m_buf->append("(\n");
    m_indent += (info.indent_delta = 4);
    break;
  case VarExport:
    if (m_indent > 0) {
      m_buf->append('\n');
      indent();
    }
    if (!m_objClass.empty()) {
      m_buf->append(m_objClass);
      m_buf->append("::__set_state(array(\n");
    } else {
      m_buf->append("array (\n");
    }
    m_indent += (info.indent_delta = 2);
    break;
  case VarDump:
  case DebugDump:
    indent();
    if (!m_rsrcName.empty()) {
      m_buf->append("resource(");
      m_buf->append(m_rsrcId);
      m_buf->append(") of type (");
      m_buf->append(m_rsrcName);
      m_buf->append(")\n");
      break;
    } else if (!m_objClass.empty()) {
      m_buf->append("object(");
      m_buf->append(m_objClass);
      m_buf->append(")#");
      m_buf->append(m_objId);
      m_buf->append(' ');
    } else {
      m_buf->append("array");
    }
    m_buf->append('(');
    m_buf->append(size);
    m_buf->append(')');

    // ...so to strictly follow PHP's output
    if (m_type == VarDump) {
      m_buf->append(" ");
    } else {
      writeRefCount();
    }

    m_buf->append("{\n");
    m_indent += (info.indent_delta = 2);
    break;
  case Serialize:
    if (!m_objClass.empty()) {
      m_buf->append("O:");
      m_buf->append((int)m_objClass.size());
      m_buf->append(":\"");
      m_buf->append(m_objClass);
      m_buf->append("\":");
      m_buf->append(size);
      m_buf->append(":{");
    } else {
      m_buf->append("a:");
      m_buf->append(size);
      m_buf->append(":{");
    }
    break;
  case JSON:
    if (info.is_vector) {
      m_buf->append("[");
    } else {
      m_buf->append("{");
    }
    break;
  default:
    ASSERT(false);
    break;
  }

  // ...so we don't mess up next array output
  if (!m_objClass.empty() || !m_rsrcName.empty()) {
    if (!m_objClass.empty()) {
      info.class_info = ClassInfo::FindClass(m_objClass.c_str());
    }
    m_objClass.clear();
    info.is_object = true;
  } else {
    info.is_object = false;
  }
}

void VariableSerializer::writePropertyPrivacy(const char *prop,
                                              const ClassInfo *cls) {
  if (!cls) return;
  const ClassInfo *origCls = cls;
  ClassInfo::PropertyInfo *p = cls->getPropertyInfo(prop);
  while (!p && cls && cls->getParentClass()) {
    cls = ClassInfo::FindClass(cls->getParentClass());
    if (cls) p = cls->getPropertyInfo(prop);
  }
  if (!p) return;
  ClassInfo::Attribute a = p->attribute;
  if (a & ClassInfo::IsProtected) {
    m_buf->append(":protected");
  } else if (a & ClassInfo::IsPrivate && cls == origCls) {
    m_buf->append(":private");
  }
}

void VariableSerializer::writeSerializedProperty(CStrRef prop,
                                                 const ClassInfo *cls) {
  String res = prop;
  const ClassInfo *origCls = cls;
  if (cls) {
    ClassInfo::PropertyInfo *p = cls->getPropertyInfo(prop.c_str());
    // Try to find defining class
    while (!p && cls && cls->getParentClass()) {
      cls = ClassInfo::FindClass(cls->getParentClass());
      if (cls) p = cls->getPropertyInfo(prop);
    }
    if (p) {
      const ClassInfo *dcls = p->owner;
      ClassInfo::Attribute a = p->attribute;
      if (a & ClassInfo::IsProtected) {
        res = String("\0*\0", 3, AttachLiteral) + prop;
      } else if (a & ClassInfo::IsPrivate && cls == origCls) {
        const char *clsname = dcls->getName();
        int clsLen = strlen(clsname);
        int headerLen = clsLen + 2;
        int totalLen = headerLen + prop.size() + 1;
        char *buf = (char*)malloc(totalLen);
        buf[0] = '\0';
        memcpy(buf + 1, clsname, clsLen);
        buf[clsLen + 1] = '\0';
        memcpy(buf + headerLen, prop.c_str(), prop.size());
        buf[totalLen - 1] = '\0';
        res = String(buf, totalLen - 1, AttachString);
      }
    }
  }
  write(res);
}

void VariableSerializer::writeArrayKey(const ArrayData *arr, Variant key) {
  ArrayInfo &info = m_arrayInfos.back();
  const ClassInfo *cls = info.class_info;
  if (info.is_object) {
    String ks(key.toString());
    if (ks.charAt(0) == '\0') {
      int span = ks.find('\0', 1);
      ASSERT(span != String::npos);
      String cl(ks.substr(1, span - 1));
      cls = ClassInfo::FindClass(cl);
      ASSERT(cls);
      key = ks.substr(span + 1);
    }
  }
  switch (m_type) {
  case PrintR: {
    indent();
    m_buf->append('[');
      String keyStr = key.toString();
      const char *p = keyStr;
      int len = keyStr.length();
      m_buf->append(p, len);
      if (info.is_object) writePropertyPrivacy(keyStr.c_str(), cls);
      m_buf->append("] => ");
    break;
  }
  case VarExport:
    indent();
    write(key, true);
    m_buf->append(" => ");
    break;
  case VarDump:
  case DebugDump:
    indent();
    if (key.isNumeric()) {
      m_buf->append('[');
      m_buf->append((const char *)key.toString());
      m_buf->append("]=>\n");
    } else {
      m_buf->append("[\"");
      String keyStr = key.toString();
      const char *p = keyStr;
      int len = keyStr.length();
      m_buf->append(p, len);
      if (info.is_object) writePropertyPrivacy(keyStr.c_str(), cls);
      m_buf->append("\"]=>\n");
    }
    break;
  case Serialize:
    if (info.is_object) {
      writeSerializedProperty(key.toString(), cls);
    } else {
      write(key);
    }
    break;
  case JSON:
    if (!info.first_element) {
      m_buf->append(",");
    }
    if (!info.is_vector) {
      write(key.toString());
      m_buf->append(":");
    }
    break;
  default:
    ASSERT(false);
    break;
  }
}

void VariableSerializer::writeArrayValue(const ArrayData *arr, CVarRef value) {
  // Do not count referenced values after the first
  if (m_type == Serialize &&
      !(value.isReferenced() &&
        m_arrayIds.find(value.getVariantData()) != m_arrayIds.end()))
    m_valueCount++;

  write(value);
  switch (m_type) {
  case PrintR:
    m_buf->append('\n');
    break;
  case VarExport:
    m_buf->append(",\n");
    break;
  default:
    break;
  }

  ArrayInfo &info = m_arrayInfos.back();
  info.first_element = false;
}

void VariableSerializer::writeArrayFooter(const ArrayData *arr) {
  ArrayInfo &info = m_arrayInfos.back();

  m_indent -= info.indent_delta;
  switch (m_type) {
  case PrintR:
    indent();
    m_buf->append(")\n");
    if (m_indent > 0) {
      m_indent -= 4;
    }
    break;
  case VarExport:
    indent();
    if (info.is_object) {
      m_buf->append("))");
    } else {
      m_buf->append(')');
    }
    break;
  case VarDump:
  case DebugDump:
    if (m_rsrcName.empty()) {
      indent();
      m_buf->append("}\n");
    }
    break;
  case Serialize:
    m_buf->append('}');
    break;
  case JSON:
    if (info.is_vector) {
      m_buf->append("]");
    } else {
      m_buf->append("}");
    }
    break;
  default:
    ASSERT(false);
    break;
  }

  m_arrayInfos.pop_back();
}

void VariableSerializer::writeSerializableObject(CStrRef clsname,
                                                 CStrRef serialized) {
  m_buf->append("C:");
  m_buf->append(clsname.size());
  m_buf->append(":\"");
  m_buf->append(clsname.data(), clsname.size());
  m_buf->append("\":");
  m_buf->append(serialized.size());
  m_buf->append(":{");
  m_buf->append(serialized.data(), serialized.size());
  m_buf->append('}');
}

///////////////////////////////////////////////////////////////////////////////

void VariableSerializer::indent() {
  for (int i = 0; i < m_indent; i++) {
    m_buf->append(' ');
  }
  if (m_referenced) {
    if (m_indent > 0 && m_type == VarDump) m_buf->append('&');
    m_referenced = false;
  }
}

bool VariableSerializer::incNestedLevel(void *ptr,
                                        bool isObject /* = false */) {
  switch (m_type) {
  case VarExport:
  case PrintR:
  case VarDump:
  case DebugDump:
    return ++m_counts[ptr] >= m_maxCount;
  case Serialize:
    {
      int ct = ++m_counts[ptr];
      if (m_arrayIds.find(ptr) != m_arrayIds.end() &&
          (m_referenced || isObject)) {
        return true;
      } else {
        m_arrayIds[ptr] = m_valueCount;
      }
      return ct >= (m_maxCount - 1);
    }
    break;
  case JSON:
    return ++m_counts[ptr] >= m_maxCount;
  default:
    ASSERT(false);
    break;
  }
  return false;
}

void VariableSerializer::decNestedLevel(void *ptr) {
  --m_counts[ptr];
}

void VariableSerializer::checkOutputSize() {
  if (m_outputLimit > 0 && m_buf->length() > m_outputLimit) {
    raise_error("Value too large for serialization");
  }
}

///////////////////////////////////////////////////////////////////////////////
}
