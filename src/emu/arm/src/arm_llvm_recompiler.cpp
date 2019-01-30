/*
 * Copyright (c) 2019 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <llvm/ExecutionEngine/Orc/CompileUtils.h>

#include <arm/arm_llvm_recompiler.h>

#include <common/algorithm.h>
#include <common/log.h>
#include <common/virtualmem.h>

#include <utility>
#include <sstream>

namespace eka2l1::arm {
    llvm::JITSymbol arm_memory_manager::findSymbol(const std::string &name) {
        // Find it in link table. Runtime functions
        // Use a reference here, so we can cache all next symbols
        auto &addr = links[name];

        if (!addr) {
            // We request LLVM to find it
            addr = llvm::RTDyldMemoryManager::getSymbolAddress(name);

            if (!addr) {
                LOG_ERROR("LLVM: Symbol {} not found, linkage failed", name);
                addr = reinterpret_cast<std::uint64_t>(null);
            }
        }

        return { addr, llvm::JITSymbolFlags::Exported };
    }

    std::uint8_t *arm_memory_manager::allocateCodeSection(std::uintptr_t size, std::uint32_t align, std::uint32_t sec_id,
        llvm::StringRef sec_name) {
        const std::lock_guard<std::mutex> guard(lock);

        // Like RPCS3, we simply allocates this
        // TODO: More nice
        std::uintptr_t sec_ptr = eka2l1::common::align(reinterpret_cast<std::uintptr_t>(*next + size), 4096);
        
        if (sec_ptr > reinterpret_cast<std::uintptr_t>(org + size)) {
            LOG_ERROR("LLVM: Out of code section memory");
            return nullptr;
        }

        common::commit(reinterpret_cast<void*>(sec_ptr), size, prot::read_write_exec);
        return std::exchange(*next, reinterpret_cast<std::uint8_t*>(sec_ptr));
    }

    std::uint8_t *arm_memory_manager::allocateDataSection(std::uintptr_t size, std::uint32_t align, std::uint32_t sec_id,
        llvm::StringRef sec_name, bool is_read_only) {
        const std::lock_guard<std::mutex> guard(lock);
        
        std::uintptr_t sec_ptr = eka2l1::common::align(reinterpret_cast<std::uintptr_t>(*next + size), 4096);
        
        if (sec_ptr > reinterpret_cast<std::uintptr_t>(org + size)) {
            LOG_ERROR("LLVM: Out of code section memory");
            return nullptr;
        }

        common::commit(reinterpret_cast<void*>(sec_ptr), size, prot::read_write);
        return std::exchange(*next, reinterpret_cast<std::uint8_t*>(sec_ptr));
    }

    arm_llvm_recompiler_base::arm_llvm_recompiler_base(
        decltype(object_layer)::GetMemoryManagerFunction get_mem_mngr_func,
        llvm::orc::JITTargetMachineBuilder jit_tmb, llvm::DataLayout dl)
        : object_layer(execution_session, get_mem_mngr_func)
        , compile_layer(execution_session, object_layer, llvm::orc::ConcurrentIRCompiler(std::move(jit_tmb)))
        , data_layout(std::move(dl))
        , mangle(execution_session, data_layout)
        , ctx(llvm::make_unique<llvm::LLVMContext>())
    {
    }

    llvm::LLVMContext &arm_llvm_recompiler_base::get_context() {
        return *ctx.getContext();
    }

    bool arm_llvm_recompiler_base::add(std::unique_ptr<llvm::Module> module) {
        llvm::Error err = compile_layer.add(execution_session.getMainJITDylib(), 
            llvm::orc::ThreadSafeModule(std::move(module), ctx));

        if (err) {
            LOG_CRITICAL("LLVM: Adding module failed");

            return false;
        }

        return true;
    }

    arm_llvm_inst_recompiler::arm_llvm_inst_recompiler(llvm::Module *module,
        decltype(object_layer)::GetMemoryManagerFunction get_mem_mngr_func,
        llvm::orc::JITTargetMachineBuilder jit_tmb, 
        llvm::DataLayout dl)
        : arm_llvm_recompiler_base(get_mem_mngr_func, jit_tmb, dl)
        , module(module) 
    {
        // Create thread context struct
        std::vector<llvm::Type*> context_struct_types;

        llvm::LLVMContext &ctx = get_context();
        llvm::Module &md = *module;

        page_table_type = 
            llvm::ArrayType::get(llvm::Type::getInt8PtrTy(ctx), 0xFFFFFFFF / 0x1000);

        // First is page table pointer for this session
        context_struct_types.emplace_back(page_table_type->getPointerTo());
        context_struct_types.emplace_back(llvm::Type::getInt32Ty(ctx));                                 // Ticks remaining (for EKA2L1)
        context_struct_types.insert(context_struct_types.end(), 16, llvm::Type::getInt32Ty(ctx));       // R0 - R15
        context_struct_types.emplace_back(llvm::Type::getInt32Ty(ctx));                                 // CPSR
        context_struct_types.insert(context_struct_types.end(), 32, llvm::Type::getFloatTy(ctx));       // D0 - D31
        context_struct_types.emplace_back(llvm::Type::getInt32Ty(ctx));                                 // FPSR

        cpu_context_type = llvm::StructType::create(ctx, context_struct_types, "arm_cpu_jit_context");

        page_table = nullptr;
    }

    llvm::Value *arm_llvm_inst_recompiler::get_mem(llvm::Value *addr, llvm::Type *type) {
        if (!page_table) {
            // Cache from struct context
            page_table = builder->CreateStructGEP(nullptr, &(*function->arg_begin()), 0);
        }

        llvm::Value *page = builder->CreateGEP(page_table, { 0, builder->CreateLShr(addr, 0x1000) });
        llvm::Value *value = 
            builder->CreateBitCast(builder->CreateGEP(page, { 0, builder->CreateAnd(addr, 0xFFF) }), type->getPointerTo());

        return value;
    }

    void arm_llvm_inst_recompiler::translate() {
        function = module->getFunction("__ftest");
        llvm::IRBuilder<> ibuilder(llvm::BasicBlock::Create(get_context(), "__condcheck"));
        builder = &ibuilder;

        // Create a CPSR condition check
        
    }

    void arm_llvm_inst_recompiler::ADD(arm_inst_ptr inst) {
        // TODO
    }
}
