#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Module.h"

#include "boomerang/core/Project.h"
#include "boomerang/core/Settings.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/proc/LibProc.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/signature/Signature.h"
#include "boomerang/ifc/IFrontEnd.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/statements/CallStatement.h"
#include "boomerang/util/log/Log.h"
#include "boomerang/ifc/IFileLoader.h"

#include <QDir>
#include <QString>


void Module::updateLibrarySignatures()
{
    m_prog->readDefaultLibraryCatalogues();

    for (Function *func : m_functionList) {
        if (func->isLib()) {
            std::shared_ptr<Signature> sig = m_prog->getLibSignature(func->getName());
            if (*sig != *func->getSignature()) {
                func->setSignature(sig);
                for (const std::shared_ptr<CallStatement> &call_stmt : func->getCallers()) {
                    call_stmt->setSigArguments();
                }
                m_prog->getProject()->alertSignatureUpdated(func);
            }
        }
    }
}


Module::Module(const QString &name, Prog *prog)
    : m_name(name)
    , m_prog(prog)
{
}


Module::~Module()
{
    for (Function *proc : m_functionList) {
        delete proc;
    }
}


size_t Module::getNumChildren() const
{
    return m_children.size();
}


Module *Module::getChild(size_t n)
{
    assert(n < getNumChildren());
    return m_children[n];
}


void Module::addChild(Module *module)
{
    if (module->m_parent) {
        module->m_parent->removeChild(module);
    }

    m_children.push_back(module);
    module->m_parent = this;
}


void Module::removeChild(Module *module)
{
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (*it == module) {
            m_children.erase(it);
            return;
        }
    }
}


Module *Module::getParentModule() const
{
    return m_parent;
}


bool Module::hasChildren() const
{
    return !m_children.empty();
}


QString Module::makeDirs() const
{
    QString path;

    if (m_parent) {
        path = m_parent->makeDirs();
    }
    else {
        path = m_prog->getProject()->getSettings()->getOutputDirectory().absolutePath();
    }

    QDir dr(path);

    if (getNumChildren() > 0 || m_parent == nullptr) {
        dr.mkpath(m_name);
        dr.cd(m_name);
    }

    return dr.absolutePath();
}


QString Module::getOutPath(const char *ext) const
{
    QString basedir = makeDirs();
    QDir dr(basedir);

    return dr.absoluteFilePath(m_name + "." + ext);
}


Module *Module::find(const QString &name)
{
    if (m_name == name) {
        return this;
    }

    for (Module *child : m_children) {
        Module *c = child->find(name);

        if (c) {
            return c;
        }
    }

    return nullptr;
}


void Module::printTree(OStream &ostr) const
{
    ostr << "\t\t" << m_name << "\n";

    for (Module *elem : m_children) {
        elem->printTree(ostr);
    }
}


void Module::setLocationMap(Address loc, Function *fnc)
{
    if (fnc == nullptr) {
        size_t count = m_labelsToProcs.erase(loc);
        Q_UNUSED(count);
        assert(count <= 1);
    }
    else {
        m_labelsToProcs[loc] = fnc;
    }
}


Function *Module::createFunction(const QString &name, Address entryAddr, bool libraryFunction)
{
    Function *function;

    if (libraryFunction) {
        function = new LibProc(entryAddr, name, this);
    }
    else {
        function = new UserProc(entryAddr, name, this);
    }

    if (Address::INVALID != entryAddr) {
        assert(m_labelsToProcs.find(entryAddr) == m_labelsToProcs.end());
        m_labelsToProcs[entryAddr] = function;
    }

    m_functionList.push_back(function); // Append this to list of procs
    m_prog->getProject()->alertFunctionCreated(function);

    if (m_prog && m_prog->getBinaryFile() && m_prog->getBinaryFile()->hasDebugInfo()) {
        if (IFileLoader *loader = m_prog->getBinaryFile()->getLoader()) {
            loader->fetchDebugInfo(function);
        }
    }
    return function;
}


Function *Module::getFunction(const QString &name) const
{
    for (Function *f : m_functionList) {
        if (f->getName() == name) {
            return f;
        }
    }

    return nullptr;
}


Function *Module::getFunction(Address entryAddr) const
{
    auto iter = m_labelsToProcs.find(entryAddr);

    return (iter != m_labelsToProcs.end()) ? iter->second : nullptr;
}
