from omniidl import idlast, idlvisitor, idlutil, idltype
from omniidl_be.cxx import output
import sys, string, copy

# Function types
eNone, eGetter, eSetter = range(3)

class CxxTypeVisitor (idlvisitor.AstVisitor):

    ttsMap = {
        idltype.tk_void:       "void",
        idltype.tk_short:      "int16_t",
        idltype.tk_long:       "int",
        idltype.tk_ushort:     "uint16_t",
        idltype.tk_ulong:      "uint32_t",
        idltype.tk_float:      "float",
        idltype.tk_double:     "double",
        idltype.tk_boolean:    "bool",
        idltype.tk_char:       "char",
        idltype.tk_octet:      "uint8_t",
        idltype.tk_any:        "any",
        idltype.tk_TypeCode:   "CORBA::TypeCode",
        idltype.tk_Principal:  "CORBA::Principal",
        idltype.tk_longlong:   "int64_t",
        idltype.tk_ulonglong:  "uint64_t",
        idltype.tk_longdouble: "long double",
        idltype.tk_wchar:      "wchar"
        }

    def visitBaseType(self, type):
        self.__result_type = self.ttsMap[type.kind()]
        self.basetype = 1
        self.__result_array = ""

    def visitDeclaredType(self, type):
        self.__result_type = idlutil.ccolonName(type.decl().scopedName())
        if type.unalias().kind() in [idltype.tk_null, idltype.tk_void, idltype.tk_short, idltype.tk_long, idltype.tk_ushort,
                        idltype.tk_ulong, idltype.tk_float, idltype.tk_double, idltype.tk_boolean,
                        idltype.tk_char, idltype.tk_octet, idltype.tk_any, idltype.tk_TypeCode,
                        idltype.tk_Principal, idltype.tk_longlong, idltype.tk_ulonglong,
                        idltype.tk_longdouble, idltype.tk_wchar, idltype.tk_enum]:
            self.basetype = 1
        else:
            self.basetype = 0

        self.__result_array = ""

    def visitStringType(self, type):
        self.basetype = 1
        self.__result_type = "string_t" 
        self.__result_array = ""

    def visitSequenceType(self, type):
        self.basetype = 0
        self.__result_type = "sequence_t_" + idlutil.ccolonName(type.seqType().decl().scopedName())
        self.__result_array = ""

    def visitDeclarator(self, node):
        l = []
        for s in node.sizes():
            l.append("[" + str(s) + "]")
        self.__result_array = string.join(l, "")

    def getResultType(self):
        self.__result_type = self.__result_type.replace(":","_");
        return self.__result_type

    def getResultArray(self):
        return self.__result_array

    def isResultBasetype(self):
        return self.basetype

# output a pure virtual class member prototype for a service operation
class CxxInterfaceVisitor (CxxTypeVisitor):

    def __init__(self, st):
        self.st = st

    def getParameterString(self, p):
        p.paramType().accept(self)
        if p.is_in() and p.is_out(): 
            inout = ""
            ptr = "*"
        elif p.is_in():
            inout = ""
            ptr = ""
        else:
            inout = ""
            ptr = "*"

        if not self.isResultBasetype():
          ptr = "*"
        type = self.getResultType()
        return (inout + type + " " + ptr + p.identifier())

    def getAllParametersString(self, node):
        paraml = []
        for p in node.parameters():
            paraml.append(self.getParameterString(p))

        return string.join(paraml, ", ")

    def getOperationName(self, node):
        return node.identifier()

    def getOperationPreAttributes(self, node):
        return ""

    def getOperationPostAttributes(self, node):
        return ""

    def getOperationTerminator(self, node):
        return ";"

    def visitOperation(self, node):
        node.returnType().accept(self)
        self.rtype = self.getResultType()

        params = self.getAllParametersString(node)

        if len(node.raises()) > 0:
            raisel = []
            for r in node.raises():
                ename  = idlutil.ccolonName(r.scopedName())
                raisel.append(ename)

            raises = " raises (" + string.join(raisel, ", ") + ")"
        else:
            raises = ""

        self.st.out("""\
@preattr@@rtype@ @id@(@params@)@postattr@@raises@@term@""",
               preattr=self.getOperationPreAttributes(node),
               postattr=self.getOperationPostAttributes(node),
               rtype=self.rtype, id=self.getOperationName(node),
               params=params, raises=raises,
               term=self.getOperationTerminator(node))

    def getReturnType(self):
        return self.rtype

# output a pure virtual class member prototype for an attribute operation get/set
class CxxAttributesInterfaceVisitor (CxxInterfaceVisitor):

    def __init__(self, st):
        CxxInterfaceVisitor.__init__(self, st)

    def getOperationName(self, node):
        if self.setter == eSetter:
            return "set_" + CxxInterfaceVisitor.getOperationName(self, node)
        else:
            return "get_" + CxxInterfaceVisitor.getOperationName(self, node)

    def getOperationPostAttributes(self, node):
        if self.setter == eSetter:
            return CxxInterfaceVisitor.getOperationPostAttributes(self, node)
        else:
            return ""

    def getParameterString(self, p):
        if self.setter == eGetter:
            return CxxInterfaceVisitor.getParameterString(self, p)
        else:
            p.paramType().accept(self)
            inout = ""
            if self.isResultBasetype() == 1:
                ptr = ""
            else:
                ptr = "*"

        type = self.getResultType()
        return (inout + type + " " + ptr + p.identifier())

    def hasOUTParam(self, node):
        for p in node.parameters():
            if p.is_out():
                return True
        return False

    def hasINOUTParam(self, node):
        for p in node.parameters():
            if p.is_in() and p.is_out():
                return True
        return False

    def visitOperation(self, node):
        # Qualify with inout param. All IN is setter only. OUT and no INOUT is getter only
        if self.hasOUTParam(node) or self.hasINOUTParam(node):
            self.visitGetOperation(node)
        if self.hasINOUTParam(node) or not self.hasOUTParam(node):
            self.visitSetOperation(node)

    def visitSetOperation(self, node):
        self.setter = eSetter
        CxxInterfaceVisitor.visitOperation(self, node)

    def visitGetOperation(self, node):
        self.setter = eGetter
        CxxInterfaceVisitor.visitOperation(self, node)

# Output a static forward declaration for an service operation
class CxxStaticVisitor (CxxInterfaceVisitor):

    def __init__(self, st):
        CxxInterfaceVisitor.__init__(self, st)

    def getOperationPreAttributes(self, node):
        return "static "

    def getOperationTerminator(self, node):
        return ";"

    def getOperationName(self, node):
        return CxxInterfaceVisitor.getOperationName(self, node) + "_unmarshal"

    def getAllParametersString(self, node):
        return "RequestMessageBuffer_t request, ResponseMessageBuffer_t response"

#    def getResultType(self):
#        return "void"

# Output a static forward declaration for an attribute operation get/set
class CxxAttributesStaticVisitor (CxxAttributesInterfaceVisitor):

    def __init__(self, st):
        CxxAttributesInterfaceVisitor.__init__(self, st)

    def getOperationPreAttributes(self, node):
        return "static "

    def getOperationPostAttributes(self, node):
        return ""

    def getOperationTerminator(self, node):
        return ";"

    def getOperationName(self, node):
        return CxxAttributesInterfaceVisitor.getOperationName(self, node) + "_unmarshal"

    def getAllParametersString(self, node):
        return "RequestMessageBuffer_t request, ResponseMessageBuffer_t response"

    def getResultType(self):
        return "void"

# Output an interface declaration for a stub service operation
class CxxStubMethodHeaderVisitor (CxxInterfaceVisitor):

    def __init__(self, st):
        CxxInterfaceVisitor.__init__(self, st)

    def getOperationTerminator(self, node):
        return ";"

# Output an interface declaration for a stub attribute operation get/set
class CxxAttributesStubMethodHeaderVisitor (CxxAttributesInterfaceVisitor):

    def __init__(self, st):
        CxxAttributesInterfaceVisitor.__init__(self, st)

    def getOperationTerminator(self, node):
        return ";"

# Output an implementation for a stub service operation
class CxxStubMethodImplVisitor (CxxInterfaceVisitor):

    def __init__(self, st, cn, func):
        CxxInterfaceVisitor.__init__(self, st)
        self.st = st
        self.cn = cn
        self.func = func

    def getOperationPreAttributes(self, node):
        return ""

    def getOperationName(self, node):
        return self.cn + "_stub" + CxxInterfaceVisitor.getOperationName(self, node)

    def getOperationTerminator(self, node):
        return ""

    def visitOperation(self, node):
        CxxInterfaceVisitor.visitOperation(self, node)
        self.func(self.st, node, eNone, self.getReturnType())

# Output an implementation for a stub attribute operation get/set
class CxxAttributesStubMethodImplVisitor (CxxAttributesInterfaceVisitor):

    def __init__(self, st, cn, func):
        CxxAttributesInterfaceVisitor.__init__(self, st)
        self.st = st
        self.cn = cn
        self.func = func

    def getOperationPreAttributes(self, node):
        return ""

    def getOperationName(self, node):
        return self.cn + "_stub::" + CxxAttributesInterfaceVisitor.getOperationName(self, node)

    def getOperationTerminator(self, node):
        return ""

    def visitGetOperation(self, node):
        CxxAttributesInterfaceVisitor.visitGetOperation(self, node)
        self.func(self.st, node, eGetter, self.getReturnType())

    def visitSetOperation(self, node):
        CxxAttributesInterfaceVisitor.visitSetOperation(self, node)
        self.func(self.st, node, eSetter, self.getReturnType())

class CxxAttributesUnmarshalVisitor (CxxAttributesStaticVisitor):

    def __init__(self, st, func):
        CxxAttributesStaticVisitor.__init__(self, st)
        self.st = st
        self.func = func

    def getOperationPreAttributes(self, node):
        return ""

    def getOperationTerminator(self, node):
        return ""

    def visitGetOperation(self, node):
        CxxAttributesStaticVisitor.visitGetOperation(self, node)
        self.func(self.st, node, eGetter)

    def visitSetOperation(self, node):
        CxxAttributesStaticVisitor.visitSetOperation(self, node)
        self.func(self.st, node, eSetter)

class CxxUnmarshalVisitor (CxxStaticVisitor):

    def __init__(self, st, func):
        CxxStaticVisitor.__init__(self, st)
        self.st = st
        self.func = func

    def getOperationPreAttributes(self, node):
        return ""

    def getOperationTerminator(self, node):
        return ""

    def visitOperation(self, node):
        CxxStaticVisitor.visitOperation(self, node)
        self.func(self.st, node, eNone)

# output a class declaration for an interface
class CxxInterfaceHeaderVisitor (idlvisitor.AstVisitor):

    def __init__(self, st):
        self.st = st

    def visitInterface(self, node):
        name = idlutil.ccolonName(node.scopedName())

        if "Attributes" == node.identifier():
            processor = CxxAttributesInterfaceVisitor(self.st)
        else:
            processor = CxxInterfaceVisitor(self.st)
        for n in node.contents():
            n.accept(processor)

# output a class declaration for a stub
class CxxStubHeaderVisitor (idlvisitor.AstVisitor):

    def __init__(self, st):
        self.st = st

    def visitInterface(self, node):
        self.st.out("struct @cn@_stub", cn = node.identifier())
        self.st.out("{")
        self.st.inc_indent()
        self.st.out("uint16_t instanceNum;")
        self.st.dec_indent()
        self.st.out("};\n")

# output a class implementation for a stub
class CxxStubImplVisitor (idlvisitor.AstVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitInterface(self, node):
        if "Attributes" == node.identifier():
            processor = CxxAttributesStubMethodImplVisitor(self.st, node.identifier(), self.func)
        else:
            processor = CxxStubMethodImplVisitor(self.st, node.identifier(), self.func)
        for n in node.contents():
            n.accept(processor)

# output a type declaration for marshalling/unmarshalling
class CxxTypeHeaderVisitor (CxxTypeVisitor):

    def __init__(self, st):
        self.st = st

    def visitStruct(self, node):
        self.outputStruct(node, "marshal")
        self.outputStruct(node, "unmarshal")

    def outputStruct(self, node, name):
        attr = ""
        ma = ""
        self.st.out("extern uint32_t @sn@_@name@(@ma@MessageBuffer_t buffer, uint32_t offset, @attr@@sn@ *value);", sn = "FirmwareUpdate__"+node.identifier(), name=name, attr=attr, ma=ma)

    def visitEnum(self, node):
        self.outputEnum(node, "marshal")
        self.outputEnum(node, "unmarshal")

    def outputEnum(self, node, name):
        attr = ""
        ma = ""
        ref = ""
        if name == "unmarshal":
            ref="*"
        
        self.st.out("extern uint32_t @tn@_@name@(@ma@MessageBuffer_t buffer, uint32_t offset, @attr@@tn@ @ref@value);", tn = "FirmwareUpdate__"+node.identifier(), name=name, attr=attr, ma=ma,ref=ref)

    def visitTypedef(self, node):
        self.outputTypedef(node, "marshal")
        self.outputTypedef(node, "unmarshal")

    def outputTypedef(self, node, name):
        attr = ""
        ma = ""

        node.aliasType().accept(self)
        type = self.getResultType()
        for d in node.declarators():
            n = 0
            idx = ""
            self.st.out("extern uint32_t @tn@_@name@(@ma@MessageBuffer_t buffer, uint32_t offset, @attr@@tn@ *value);", tn="FirmwareUpdate__"+d.identifier(), name=name, attr=attr, ma=ma)

# output a type implementation for marshalling/unmarshalling
class CxxTypeImplVisitor (CxxTypeVisitor):

    def __init__(self, st):
        self.st = st

    def visitStruct(self, node):
        self.outputStruct(node, "marshal")
        self.outputStruct(node, "unmarshal")

    def outputStruct(self, node, name):
        attr = ""
        ma = ""
        self.st.out("uint32_t @sn@_@name@(@ma@MessageBuffer_t buffer, uint32_t offset, @attr@@sn@ *value)", sn = "FirmwareUpdate__" + node.identifier(), name=name, attr=attr, ma=ma)
        self.st.out("{")
        self.st.inc_indent()
        self.st.out("uint32_t structOffset = 4;")
        for m in node.members():
            m.memberType().accept(self)
            type = self.getResultType()
            type = type.replace("<","_");
            type = type.replace(">","_");
            type = type.replace(":","_");
            for d in m.declarators():
                n = 0
                idx = ""
                for s in d.sizes():
                    self.st.out("for (uint32_t i@n@=0; i@n@<@s@; i@n@++)", n=n, s=s)
                    self.st.out("{")
                    self.st.inc_indent()
                    idx = idx + "[i" + str(n) + "]"
                    n=n+1
                self.st.out("structOffset += @type@_@name@(buffer, offset+structOffset, value.@id@@idx@);", type=type, id=d.identifier(), name=name, idx=idx)
                for s in d.sizes():
                    self.st.dec_indent()
                    self.st.out("}")
        if (name == "marshal"):
            self.st.out("(void) uint32_t (buffer, offset, structOffset); // struct length")
        self.st.out("return structOffset;")
        self.st.dec_indent()
        self.st.out("}\n")

    def visitEnum(self, node):
        self.outputEnum(node, "marshal")
        self.outputEnum(node, "unmarshal")

    def outputEnum(self, node, name):
        attr = ""
        ma = ""
        ref = ""
        if name == "unmarshal":
            ref="*"
        self.st.out("uint32_t @tn@_@name@(@ma@MessageBuffer_t buffer, uint32_t offset, @attr@@tn@ @ref@value)", tn = "FirmwareUpdate__"+node.identifier(), name=name, attr=attr, ma=ma,ref=ref)
        self.st.out("{")
        self.st.inc_indent()
        if name == "marshal":
            self.st.out("uint32_t valueInt = (uint32_t)value;")
            self.st.out("return uint32_t_@name@(buffer, offset, valueInt);", name=name)
        else:
            self.st.out("uint32_t valueInt;")
            self.st.out("uint32_t size = uint32_t_@name@(buffer, offset, &valueInt);", name=name)
            self.st.out("*value = (@tn@)valueInt;",tn="FirmwareUpdate__"+node.identifier())
            self.st.out("return size;")
        self.st.dec_indent()
        self.st.out("}\n")

    def visitTypedef(self, node):
        self.outputTypedef(node, "marshal")
        self.outputTypedef(node, "unmarshal")

    def outputTypedef(self, node, name):
        attr = ""
        ma = ""

        node.aliasType().accept(self)
        type = self.getResultType()
        type = type.replace("<","_");
        type = type.replace(">","_");
        type = type.replace(":","_");
        for d in node.declarators():
            n = 0
            idx = ""
            self.st.out("uint32_t @tn@_@name@(@ma@MessageBuffer_t buffer, uint32_t offset, @attr@@tn@ *value)", tn="FirmwareUpdate__"+d.identifier(), name=name, attr=attr, ma=ma)
            self.st.out("{")
            self.st.inc_indent()
            self.st.out("uint32_t typeOffset = 0;")
            for s in d.sizes():
                self.st.out("for (uint32_t i@n@=0; i@n@<@s@; i@n@++)", n=n, s=s)
                self.st.out("{")
                self.st.inc_indent()
                idx = idx + "[i" + str(n) + "]"
                n=n+1
            self.st.out("typeOffset += @type@_@name@(buffer, offset + typeOffset, value@idx@);", type=type, name=name, idx=idx)
            for s in d.sizes():
                self.st.dec_indent()
                self.st.out("}")
            self.st.out("return typeOffset;")
            self.st.dec_indent()
            self.st.out("}\n")

# output a type
class CxxTypeDeclVisitor (CxxTypeVisitor):

    def __init__(self, st):
        self.st = st

    def visitStruct(self, node):
        self.st.out("typedef struct")
        self.st.out("{")
        self.st.inc_indent()
        for m in node.members():
            if m.constrType():
                m.memberType().decl().accept(self)
            m.memberType().accept(self)
            type = self.getResultType()
            for d in m.declarators():
                d.accept(self)
                arr = self.getResultArray()
                self.st.out("@type@ @id@@arr@;", type=type, id=d.identifier(), arr=arr)
        self.st.dec_indent()
        self.st.out("} @sn@;\n", sn=node.identifier())

    def visitEnum(self, node):
        self.st.out("typedef enum")
        self.st.out("{")
        self.st.inc_indent()
        enuml = []
        for e in node.enumerators(): enuml.append(e.identifier())
        self.st.out(string.join(enuml, ", "))
        self.st.dec_indent()
        self.st.out("} FirmwareUpdate__@en@;\n", en=node.identifier())

    def visitTypedef(self, node):
        if node.constrType():
            node.aliasType().decl().accept(self)

        node.aliasType().accept(self)
        type = self.getResultType()
        for d in node.declarators():
            d.accept(self)
            arr = self.getResultArray()
            self.st.out("typedef @type@ FirmwareUpdate__@id@@arr@;\n", type=type, id=d.identifier(), arr=arr);

class CxxInterfaceUnmarshalStaticForwardVisitor (idlvisitor.AstVisitor):

    def __init__(self, st):
        self.st = st

    def visitInterface(self, node):
        name = idlutil.ccolonName(node.scopedName())

        if "Attributes" == node.identifier():
            processor = CxxAttributesStaticVisitor(self.st)
        else:
            processor = CxxStaticVisitor(self.st)
        for n in node.contents():
            n.accept(processor)

class CxxInterfaceUnmarshalStaticBodyVisitor (idlvisitor.AstVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitInterface(self, node):
        name = idlutil.ccolonName(node.scopedName())

        if "Attributes" == node.identifier():
            processor = CxxAttributesUnmarshalVisitor(self.st, self.func)
        else:
            processor = CxxUnmarshalVisitor(self.st, self.func)
        for n in node.contents():
            n.accept(processor)

# output case statement entries for a single service marshal function
class CxxServiceCodeCaseInterfaceVisitor (idlvisitor.AstVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitOperation(self, node):
        self.func(self.st, "k_SC_" + node.identifier(), node.identifier() + "_unmarshal")

# output case statement entries for a single attribute get marshal function
class CxxAttributeCodeGetCaseInterfaceVisitor (CxxAttributesInterfaceVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitOperation(self, node):
        # Qualify with inout param. All IN is setter only. OUT and no INOUT is getter only
        if self.hasOUTParam(node) or self.hasINOUTParam(node):
            self.func(self.st, "k_AC_" + node.identifier(), "get_" + node.identifier() + "_unmarshal")

# output case statement entries for a single attribute get marshal function
class CxxAttributeCodeSetCaseInterfaceVisitor (CxxAttributesInterfaceVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitOperation(self, node):
        # Qualify with inout param. All IN is setter only. OUT and no INOUT is getter only
        if self.hasINOUTParam(node) or not self.hasOUTParam(node):
            self.func(self.st, "k_AC_" + node.identifier(), "set_" + node.identifier() + "_unmarshal")

# output case statement entries for picking the correct service marshal functions
class CxxInterfaceUnmarshalBodyVisitor (idlvisitor.AstVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitInterface(self, node):
        name = idlutil.ccolonName(node.scopedName())

        if "Attributes" != node.identifier():
            processor = CxxServiceCodeCaseInterfaceVisitor(self.st, self.func)
            for n in node.contents():
                n.accept(processor)

# output case statement entries for picking the correct attribute marshal functions
class CxxInterfaceGetUnmarshalBodyVisitor (idlvisitor.AstVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitInterface(self, node):
        name = idlutil.ccolonName(node.scopedName())

        if "Attributes" == node.identifier():
            processor = CxxAttributeCodeGetCaseInterfaceVisitor(self.st, self.func)
            for n in node.contents():
                n.accept(processor)

# output case statement entries for picking the correct attribute marshal functions
class CxxInterfaceSetUnmarshalBodyVisitor (idlvisitor.AstVisitor):

    def __init__(self, st, func):
        self.st = st
        self.func = func

    def visitInterface(self, node):
        name = idlutil.ccolonName(node.scopedName())

        if "Attributes" == node.identifier():
            processor = CxxAttributeCodeSetCaseInterfaceVisitor(self.st, self.func)
            for n in node.contents():
                n.accept(processor)

class CxxTreeVisitor (idlvisitor.AstVisitor):

    def visitAST(self, node):
        for n in node.declarations():
            n.accept(self)

    def visitModule(self, node):
        if not node.mainFile():
            return

        # Output the interface header
        st = output.Stream(output.createFile(node.identifier() + ".h"), 2)
        self.outputGenCode(st)
        st.out("#ifndef __@nsu@_H__", nsu = node.identifier().upper())
        st.out("#define __@nsu@_H__\n", nsu = node.identifier().upper())
        st.out("#include \"message-buffer.h\"\n");
        st.out("#include <linux/types.h>\n");
        st.inc_indent()
        visitor = CxxTypeDeclVisitor(st)
        for n in node.definitions():
            n.accept(visitor)
        visitor = CxxInterfaceHeaderVisitor(st)
        for n in node.definitions():
            n.accept(visitor)
        st.dec_indent()
        st.out("#endif // __@nsu@_H__\n", nsu = node.identifier().upper())
        st.close()

        # Output the unmarshal header
        st = output.Stream(output.createFile(node.identifier() + "_unmarshal.h"), 2)
        self.outputGenCode(st)
        st.out("#ifndef __@nsu@_UNMARSHAL_H__", nsu = node.identifier().upper())
        st.out("#define __@nsu@_UNMARSHAL_H__\n", nsu = node.identifier().upper())
        st.out("#include \"message-buffer.h\"\n")
        st.out("#include \"FirmwareUpdate.h\"\n")
        st.out("#include <linux/types.h>\n");
        st.inc_indent()
        st.out("extern FirmwareUpdate__ErrorCode  unmarshal(RequestMessageBuffer_t request, ResponseMessageBuffer_t response);\n")
        st.dec_indent()
        st.out("#endif // __@nsu@_UNMARSHAL_H__\n", nsu = node.identifier().upper())
        st.close()

        # Output the unmarshal implementation
        st = output.Stream(output.createFile(node.identifier() + "_unmarshal.c"), 2)
        self.outputGenCode(st)
        st.out("#include \"@ns@_unmarshal.h\"", ns = node.identifier())
        st.out("#include \"@ns@.h\"\n", ns = node.identifier())
        st.out("#include \"@ns@_type.h\"\n", ns = node.identifier())
        st.inc_indent()
        st.out("")
        # Forward declarations
        st.out("static FirmwareUpdate__ErrorCode get_unmarshal(RequestMessageBuffer_t request, ResponseMessageBuffer_t response);");
        st.out("static FirmwareUpdate__ErrorCode set_unmarshal(RequestMessageBuffer_t request, ResponseMessageBuffer_t response);");
        visitor = CxxInterfaceUnmarshalStaticForwardVisitor(st)
        for n in node.definitions():
            n.accept(visitor)
        st.out("")
        # Common unmarshal functions
        self.outputUnmarshal(st, node)
        self.outputGetUnmarshal(st, node)
        self.outputSetUnmarshal(st, node)
        # Individual unmarshal functions
        visitor = CxxInterfaceUnmarshalStaticBodyVisitor(st, self.outputUnmarshalBody)
        for n in node.definitions():
            n.accept(visitor)
        st.dec_indent()
        st.close()

        # Output the stub header
        st = output.Stream(output.createFile(node.identifier() + "_stub.h"), 2)
        self.outputGenCode(st)
        visitor = CxxStubHeaderVisitor(st)
        st.out("#ifndef __@nsu@_STUB_H__", nsu = node.identifier().upper())
        st.out("#define __@nsu@_STUB_H__\n", nsu = node.identifier().upper())
        st.out("#include \"message-buffer.h\"\n");
        st.out("#include <linux/types.h>\n");
        st.out("#include \"@ns@.h\"\n", ns = node.identifier())
        st.inc_indent()
        for n in node.definitions():
            n.accept(visitor)
        st.dec_indent()
        st.out("#endif // __@nsu@_STUB_H__\n", nsu = node.identifier().upper())
        st.close()

        # Output the stub implementation
        st = output.Stream(output.createFile(node.identifier() + "_stub.c"), 2)
        self.outputGenCode(st)
        visitor = CxxStubImplVisitor(st, self.outputStubBody)
        st.out("#include \"@ns@_stub.h\"\n", ns = node.identifier())
        st.out("#include \"@ns@.h\"\n", ns = node.identifier())
        st.out("#include \"@ns@_type.h\"\n", ns = node.identifier())
        st.inc_indent()
        st.out("ClassCode k_CC = k_CC_@cc@;\n", cc = node.identifier())
        for n in node.definitions():
            n.accept(visitor)
        st.dec_indent()
        st.close()

        # Output the type header
        st = output.Stream(output.createFile(node.identifier() + "_type.h"), 2)
        self.outputGenCode(st)
        visitor = CxxTypeHeaderVisitor(st)
        st.out("#ifndef __@nsu@_TYPE_H__", nsu = node.identifier().upper())
        st.out("#define __@nsu@_TYPE_H__\n", nsu = node.identifier().upper())
        st.out("#include \"message-buffer.h\"\n");
        st.out("#include <linux/types.h>\n");
        st.out("#include \"@ns@.h\"\n", ns = node.identifier())
        st.inc_indent()
        for n in node.definitions():
            n.accept(visitor)
            st.out("")
        st.dec_indent()
        st.out("#endif // __@nsu@_TYPE_H__\n", nsu = node.identifier().upper())
        st.close()

        # Output the type implementation
        st = output.Stream(output.createFile(node.identifier() + "_type.c"), 2)
        self.outputGenCode(st)
        visitor = CxxTypeImplVisitor(st)
        st.out("#include \"@ns@_type.h\"\n", ns = node.identifier())
        st.out("#include \"@ns@.h\"\n", ns = node.identifier())
        st.inc_indent()
        for n in node.definitions():
            n.accept(visitor)
        st.dec_indent()
        st.close()


    def outputGenCode(self, st):
        st.out("///////////////////////////////////////////////////////////////")
        st.out("// This file is generated by the Lab X omniIDL C back-end. //")
        st.out("// Any modifications to this file will be overwritten.       //")
        st.out("///////////////////////////////////////////////////////////////\n")

    def outputCaseEntry(self, st, attr, fname):
        st.out("case @attr@:", attr=attr)
        st.inc_indent()
        st.out("return @fname@(request, response);", fname=fname)
        st.out("break;\n")
        st.dec_indent()

    def outputGetUnmarshal(self, st, node):
        st.out("FirmwareUpdate__ErrorCode get_unmarshal(RequestMessageBuffer_t request, ResponseMessageBuffer_t response)")
        st.out("{")
        st.inc_indent()
        st.out("switch(getAttributeCode_req(request))")
        st.out("{")
        st.inc_indent()
        visitor = CxxInterfaceGetUnmarshalBodyVisitor(st, self.outputCaseEntry)
        for n in node.definitions():
            n.accept(visitor)
        st.out("default:")
        st.inc_indent()
        st.out("return e_EC_INVALID_ATTRIBUTE_CODE;");
        st.dec_indent()
        st.dec_indent()
        st.out("}")
        st.dec_indent()
        st.out("}\n")

    def outputSetUnmarshal(self, st, node):
        st.out("FirmwareUpdate__ErrorCode set_unmarshal(RequestMessageBuffer_t request, ResponseMessageBuffer_t response)")
        st.out("{")
        st.inc_indent()
        st.out("switch(getAttributeCode_req(request))")
        st.out("{")
        st.inc_indent()
        visitor = CxxInterfaceSetUnmarshalBodyVisitor(st, self.outputCaseEntry)
        for n in node.definitions():
            n.accept(visitor)
        st.out("default:")
        st.inc_indent()
        st.out("return e_EC_INVALID_ATTRIBUTE_CODE;");
        st.dec_indent()
        st.dec_indent()
        st.out("}")
        st.dec_indent()
        st.out("}\n")

    def outputUnmarshal(self, st, node):
        st.out("FirmwareUpdate__ErrorCode unmarshal(RequestMessageBuffer_t request, ResponseMessageBuffer_t response)") # unmarshal
        st.out("{")
        st.inc_indent()
        st.out("switch(getServiceCode_req(request))")
        st.out("{")
        st.inc_indent()
        self.outputCaseEntry(st, "k_SC_getAttribute", "get_unmarshal") 
        self.outputCaseEntry(st, "k_SC_setAttribute", "set_unmarshal") 
        visitor = CxxInterfaceUnmarshalBodyVisitor(st, self.outputCaseEntry)
        for n in node.definitions():
            n.accept(visitor)
        st.out("default:")
        st.inc_indent()
        st.out("return e_EC_INVALID_SERVICE_CODE;");
        st.dec_indent()
        st.dec_indent()
        st.out("}")
        st.dec_indent()
        st.out("}\n")

    def outputUnmarshalBody(self, st, node, setter):
        st.out("{")
        st.inc_indent()
        # Check instance
        if setter == eNone:
            obj = "Services"
        if (setter == eSetter) or (setter == eGetter):
            obj = "Attributes"
            
        # Unmarshal input parameter data
        st.out("uint32_t offset = getPayloadOffset_req(request);")
        for p in node.parameters():
            self.outputParameterUnmarshal(st, p, "request")
        # Make the function call
        rtvisitor = CxxTypeVisitor()
        node.returnType().accept(rtvisitor)
        if setter == eNone:
            fname = node.identifier()
        if setter == eSetter:
            fname = "set_" + node.identifier()
        if setter == eGetter:
            fname = "get_" + node.identifier()
        paraml = []
        for p in node.parameters():
            ref = ""
            if (setter == eGetter) and (p.is_out()):
                ref = "&"
            paramVisitor = CxxTypeVisitor()
            p.paramType().accept(paramVisitor)
            if (not paramVisitor.isResultBasetype()):
              ref = "&"
            paraml.append(ref + p.identifier())

        st.out("@rt@ retval = @fname@(@params@);",
            rt=rtvisitor.getResultType(), fname=fname, params=string.join(paraml, ", "))

        # Marshal the output parameter data
        st.out("offset = getPayloadOffset_resp(response);")
        for p in node.parameters():
            self.outputParameterMarshal(st, p, setter, "response")
        st.out("setLength_resp(response,offset);")
        st.out("return retval;")
        st.dec_indent()
        st.out("}\n")

    def outputParameterUnmarshal(self, st, p, bn):
        ptvisitor = CxxTypeVisitor()
        p.paramType().accept(ptvisitor)
        st.out("@pt@ @pn@;", pt=ptvisitor.getResultType(), pn=p.identifier())
        if p.is_in():
            st.out("offset += @pt@_unmarshal(@bn@, offset, &@pn@);", pt=ptvisitor.getResultType(), pn=p.identifier(), bn=bn)

    def outputParameterMarshal(self, st, p, setter, bn):
        # inout params are for just input on the setter
        ptvisitor = CxxTypeVisitor()
        p.paramType().accept(ptvisitor)
        if p.is_in() and (setter == eSetter):
            return
        if p.is_out():
            st.out("offset += @pt@_marshal(@bn@, offset, @pn@);", pt=ptvisitor.getResultType(), pn=p.identifier(), bn=bn)

    def outputStubBody(self, st, node, setter, returnType):
        st.out("{")
        st.inc_indent()
        st.out("RequestMessageBuffer_t request;")
        st.out("ResponseMessageBuffer_t response;")

        # Fill in request header fields
        st.out("setClassCode_req(request,k_CC);")
        st.out("setInstanceNumber_req(request,0);")
        if (setter == eNone):
            st.out("setServiceCode_req(request,k_SC_@sc@);", sc=node.identifier())
            st.out("setAttributeCode_req(request,0);")
        if (setter == eSetter):
            st.out("setServiceCode_req(request,k_SC_setAttribute);")
            st.out("setAttributeCode_req(request,k_AC_@ac@);", ac=node.identifier())
        if (setter == eGetter):
            st.out("setServiceCode_req(request,k_SC_getAttribute);")
            st.out("setAttributeCode_req(request,k_AC_@ac@);", ac=node.identifier())

        # Marshal request parameter data
        st.out("uint32_t offset = getPayloadOffset_req(request);")
        for p in node.parameters():
            self.outputParameterStubMarshal(st, p, setter, "request")

        st.out("setLength_req(request,offset);")

        # Send the message and get the response
        st.out("SendMessage(request, response);")

        # Unmarshal response parameter data
        st.out("offset = getPayloadOffset_resp(response);")
        for p in node.parameters():
            self.outputParameterStubUnmarshal(st, p, setter, "response")

        st.out("return (@type@)getStatusCode_resp(response);", type=returnType)
        st.dec_indent()
        st.out("}\n")

    def outputParameterStubMarshal(self, st, p, setter, bn):
        ptvisitor = CxxTypeVisitor()
        p.paramType().accept(ptvisitor)
        deref = ""
        if p.is_out() and (((1 != ptvisitor.isResultBasetype()) and (setter != eSetter)) or (setter == eGetter)):
            deref = "*"
        if p.is_in():
            st.out("offset += @pt@_marshal(@bn@, offset, @deref@@pn@);", pt=ptvisitor.getResultType(), pn=p.identifier(), bn=bn, deref=deref)

    def outputParameterStubUnmarshal(self, st, p, setter, bn):
        ptvisitor = CxxTypeVisitor()
        p.paramType().accept(ptvisitor)
        deref = ""
        if (setter == eGetter):
            deref = "*"
        if p.is_in() and (setter == eSetter):
            return
        if p.is_out():
            st.out("offset += @pt@_unmarshal(@bn@, offset, @deref@@pn@);", pt=ptvisitor.getResultType(), pn=p.identifier(), bn=bn, deref=deref)

def run(tree, args):
    visitor = CxxTreeVisitor()
    tree.accept(visitor)

# vi:set ai sw=4 expandtab ts=4:

