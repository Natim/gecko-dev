/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_SelfHosting_h_
#define vm_SelfHosting_h_

#include "jsapi.h"
#include "NamespaceImports.h"

#include "vm/Stack.h"

class JSAtom;

namespace js {

/*
 * Check whether the given JSFunction is a self-hosted function whose
 * self-hosted name is the given name.
 */
bool
IsSelfHostedFunctionWithName(JSFunction* fun, JSAtom* name);

bool
IsCallSelfHostedNonGenericMethod(NativeImpl impl);

bool
ReportIncompatibleSelfHostedMethod(JSContext* cx, const CallArgs& args);

/* Get the compile options used when compiling self hosted code. */
void
FillSelfHostingCompileOptions(JS::CompileOptions& options);

bool
CallSelfHostedFunction(JSContext* cx, char const* name, InvokeArgs& args);

bool
CallSelfHostedFunction(JSContext* cx, HandlePropertyName name, InvokeArgs& args);

} /* namespace js */

#endif /* vm_SelfHosting_h_ */
