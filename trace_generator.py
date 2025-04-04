import tkinter as tk
from tkinter import messagebox
import random

# 生成trace文件的函数
def generate_trace_file():
    try:
        # 获取用户输入的参数
        trace_file_name = trace_file_name_entry.get() or "user_trace.trace"
        max_address = int(max_address_entry.get())
        num_instructions = int(num_instructions_entry.get())
        
        # 验证最大地址空间是否为正整数
        if max_address <= 0:
            messagebox.showerror("Invalid input", "Max address space must be a positive integer.")
            return
        
        # 验证预期命令数量是否为正整数
        if num_instructions <= 0:
            messagebox.showerror("Invalid input", "Expected instruction count must be a positive integer.")
            return
        
        # 获取比例输入
        write_instruction_ratio = float(write_instruction_ratio_entry.get())
        read_instruction_ratio = 100 - write_instruction_ratio
        
        # 获取顺序/随机指令比例
        read_sequential_ratio = float(read_sequential_ratio_entry.get()) / 100
        write_sequential_ratio = float(write_sequential_ratio_entry.get()) / 100

        # 获取顺序块大小（每个块的大小）
        block_size = int(block_size_entry.get())

        # 获取顺序块数量
        sequential_block_count = int(sequential_block_count_entry.get())

        # 获取块对齐选项
        align_to_block_size = align_to_block_size_var.get()

        # 计算指令数量
        sw_count = int(num_instructions * write_instruction_ratio * write_sequential_ratio // 100)
        sr_count = int(num_instructions * read_instruction_ratio * read_sequential_ratio // 100)
        rw_count = int(num_instructions * write_instruction_ratio // 100) - sw_count
        rr_count = int(num_instructions * read_instruction_ratio // 100) - sr_count
        sw_count_output = sw_count
        sr_count_output = sr_count
        rw_count_output = rw_count
        rr_count_output = rr_count

        # 更新比例显示
        read_write_label.config(text=f"Read Instruction Ratio: {read_instruction_ratio:.1f}%")
        read_sequential_label.config(text=f"Read Random Instruction Ratio: {100 - read_sequential_ratio * 100:.1f}%")
        write_sequential_label.config(text=f"Write Random Instruction Ratio: {100 - write_sequential_ratio * 100:.1f}%")

        # 生成trace文件内容
        trace_data = []
        
        while sw_count > 0 or sr_count > 0 or rw_count > 0 or rr_count > 0:
            if sw_count > 0:
                addr = random.randint(0, max_address - block_size * sequential_block_count)
                if align_to_block_size:
                    addr = (addr // block_size) * block_size
                for i in range(sequential_block_count):
                    trace_data.append(f"1 {addr + i * block_size}")
                sw_count -= sequential_block_count

            if rr_count > 0:
                addr = random.randint(0, max_address - block_size)
                trace_data.append(f"0 {addr}")
                rr_count -= 1

            if sr_count > 0:
                addr = random.randint(0, max_address - block_size * sequential_block_count)
                if align_to_block_size:
                    addr = (addr // block_size) * block_size
                for i in range(sequential_block_count):
                    trace_data.append(f"0 {addr + i * block_size}")
                sr_count -= sequential_block_count

            if rw_count > 0:
                addr = random.randint(0, max_address - block_size)
                if align_to_block_size:
                    addr = (addr // block_size) * block_size
                trace_data.append(f"1 {addr}")
                rw_count -= 1

        # 保存生成的trace文件
        try:
            with open(trace_file_name, "w") as file:
                for trace in trace_data:
                    file.write(trace + "\n")
            messagebox.showinfo("Success", f"Trace file {trace_file_name} has been generated successfully!\n"
                                         f"SW commands: {sw_count_output}\nSR commands: {sr_count_output}\n"
                                         f"RW commands: {rw_count_output}\nRR commands: {rr_count_output}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to write trace file: {e}")

    except ValueError:
        messagebox.showerror("Invalid input", "Please enter valid integers for address space and instruction count.")

# 更新比例的显示函数
def update_labels():
    try:
        # 获取用户输入的比例
        write_instruction_ratio = float(write_instruction_ratio_entry.get())
        read_instruction_ratio = 100 - write_instruction_ratio
        
        # 获取顺序/随机指令比例
        read_sequential_ratio = float(read_sequential_ratio_entry.get()) / 100
        write_sequential_ratio = float(write_sequential_ratio_entry.get()) / 100

        # 更新比例显示
        read_write_label.config(text=f"Read Instruction Ratio: {read_instruction_ratio:.1f}%")
        read_sequential_label.config(text=f"Read Random Instruction Ratio: {100 - read_sequential_ratio * 100:.1f}%")
        write_sequential_label.config(text=f"Write Random Instruction Ratio: {100 - write_sequential_ratio * 100:.1f}%")
    except ValueError:
        pass  # 忽略无效的输入，直到用户正确输入数字

# GUI设置
root = tk.Tk()
root.title("Trace File Generator")

# 设置GUI布局
tk.Label(root, text="Trace File Name (default: user_trace.trace)").grid(row=0, column=0)
trace_file_name_entry = tk.Entry(root)
trace_file_name_entry.insert(0, "user_trace.trace")  # 默认填入文件名
trace_file_name_entry.grid(row=0, column=1)

tk.Label(root, text="Max Address Space").grid(row=1, column=0)
max_address_entry = tk.Entry(root)
max_address_entry.grid(row=1, column=1)

tk.Label(root, text="Expected Instruction Count").grid(row=2, column=0)
num_instructions_entry = tk.Entry(root)
num_instructions_entry.grid(row=2, column=1)

tk.Label(root, text="Block Size").grid(row=3, column=0)
block_size_entry = tk.Entry(root)
block_size_entry.grid(row=3, column=1)

tk.Label(root, text="Sequential Block Count (number of blocks)").grid(row=4, column=0)
sequential_block_count_entry = tk.Entry(root)
sequential_block_count_entry.grid(row=4, column=1)

tk.Label(root, text="Write Instruction Ratio (0-100)").grid(row=5, column=0)
write_instruction_ratio_entry = tk.Entry(root)
write_instruction_ratio_entry.grid(row=5, column=1)
write_instruction_ratio_entry.insert(0, "50")  # 默认值
write_instruction_ratio_entry.bind("<KeyRelease>", lambda e: update_labels())  # 更新比例显示

# 显示比例
read_write_label = tk.Label(root, text="Read Instruction Ratio: 50.0%")
read_write_label.grid(row=5, column=2)

tk.Label(root, text="Read Sequential Instruction Ratio (0-100)").grid(row=6, column=0)
read_sequential_ratio_entry = tk.Entry(root)
read_sequential_ratio_entry.grid(row=6, column=1)
read_sequential_ratio_entry.insert(0, "50")  # 默认值
read_sequential_ratio_entry.bind("<KeyRelease>", lambda e: update_labels())  # 更新比例显示

read_sequential_label = tk.Label(root, text="Read Random Instruction Ratio: 50.0%")
read_sequential_label.grid(row=6, column=2)

tk.Label(root, text="Write Sequential Instruction Ratio (0-100)").grid(row=7, column=0)
write_sequential_ratio_entry = tk.Entry(root)
write_sequential_ratio_entry.grid(row=7, column=1)
write_sequential_ratio_entry.insert(0, "50")  # 默认值
write_sequential_ratio_entry.bind("<KeyRelease>", lambda e: update_labels())  # 更新比例显示

write_sequential_label = tk.Label(root, text="Write Random Instruction Ratio: 50.0%")
write_sequential_label.grid(row=7, column=2)

# 添加块对齐选项
align_to_block_size_var = tk.BooleanVar()
align_to_block_size_check = tk.Checkbutton(root, text="Align to Block Size", variable=align_to_block_size_var)
align_to_block_size_check.grid(row=8, columnspan=3)

# 生成trace文件按钮
generate_button = tk.Button(root, text="Generate Trace File", command=generate_trace_file)
generate_button.grid(row=9, columnspan=3)

root.mainloop()
