// Print inbound xrefs and their containing functions for explicitly supplied addresses.
// This is read-only and runs against an existing Ghidra project.
// @category MTvGPU
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class TraceDriverReferences extends GhidraScript {
    @Override public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address target = toAddr(arg);
            println("TARGET " + target + " " + getSymbolAt(target));
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
            while (refs.hasNext()) {
                monitor.checkCancelled();
                Reference ref = refs.next();
                Address from = ref.getFromAddress();
                Function f = getFunctionContaining(from);
                Instruction ins = getInstructionAt(from);
                println("  FROM " + from + " type=" + ref.getReferenceType() +
                        " operand=" + ref.getOperandIndex() +
                        " function=" + (f == null ? "<data>" : f.getEntryPoint() + " " + f.getName()) +
                        " instruction=" + (ins == null ? "<none>" : ins.toString()));
            }
        }
    }
}
