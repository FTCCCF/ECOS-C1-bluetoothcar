// Execute the actual RV32 binary. The explicit assertions remain enabled;
// --no-assert only suppresses incompatible annotations in the old CPU source.
module tb;
  reg clk=0; always #5 clk=~clk;
  reg resetn=0;
  wire valid,instr,trap; wire [31:0] addr,wdata; wire [3:0] wstrb;
  reg [31:0] rdata=0; reg ready=0;
  reg [31:0] flash[0:131071],ram[0:32767];
  reg [15:0] gpio=0; reg [31:0] ddr=32'hffff;
  reg sd=0,old_ws=0,old_sck=0;
  integer frame_id=0,bit_index=0,rises=0,cycles=0;
  integer capture_mode=0,capture_started=0,wait_cycles=0,pending=0,left_wait=0;
  integer stop_frame=-1,stop_sent=0;
  integer min_period=1000000,max_period=0,last_rise=0;
  string image,line="",label,decision;
  integer probability,infer_cycles,cr2,cr1;
  function automatic [23:0] pcm(input integer f,input bit right);
    if(capture_mode==2)begin
      if(right||(f<4096&&(f<1024||f>=1280)))pcm=0;
      else if(f%2)pcm=24'hf44800;else pcm=24'h0bb800;
    end else if(right)pcm=24'hfedcba+f;else if(f%2)pcm=24'h123456+f;else pcm=24'h876543+f;
  endfunction
  always @(gpio) begin
    if(gpio[8]!==old_ws)begin if(!gpio[8])frame_id=frame_id+1;bit_index=0;end
    else if(old_sck&&!gpio[9])bit_index=bit_index+1;
    if(bit_index<24)sd=(pcm(frame_id,gpio[8])>>(23-bit_index))&1;else sd=0;
    old_ws=gpio[8];old_sck=gpio[9];
  end
  always @(posedge gpio[9])begin
    rises=rises+1;
    if(last_rise>0)begin
      if(cycles-last_rise<min_period)min_period=cycles-last_rise;
      if(cycles-last_rise>max_period)max_period=cycles-last_rise;
    end
    last_rise=cycles;
  end
  picorv32 #(.BARREL_SHIFTER(1),.COMPRESSED_ISA(1),.ENABLE_MUL(1),.ENABLE_FAST_MUL(1),.ENABLE_DIV(1),.ENABLE_IRQ(0),.PROGADDR_RESET(32'h30000000)) cpu
  (.clk(clk),.resetn(resetn),.trap(trap),.mem_valid(valid),.mem_instr(instr),.mem_addr(addr),.mem_wdata(wdata),.mem_wstrb(wstrb),.mem_rdata(rdata),.mem_ready(ready),.irq(32'b0));
  always @(posedge clk)begin : bus
    reg [31:0] v; integer i,index;
    cycles<=cycles+1;ready<=0;
    if(trap)$fatal(1,"CPU trap pc=%h",cpu.reg_pc);
    if(cycles>1500000000)$fatal(1,"CPU timeout pc=%h",cpu.reg_pc);
    if(valid&&!ready)begin
      if(!pending&&wait_cycles>0)begin pending=1;left_wait=wait_cycles;end
      else if(left_wait>1)left_wait=left_wait-1;
      else begin
        pending=0;ready<=1;v=0;
        if(addr>=32'h30000000&&addr<32'h30080000)v=flash[(addr-32'h30000000)>>2];
        else if(addr<32'h20000)begin
          index=addr>>2;v=ram[index];
          for(i=0;i<4;i=i+1)if(wstrb[i])ram[index][8*i+:8]=wdata[8*i+:8];
        end else if(addr==32'h03000000)begin
          v={16'b0,gpio};v[5]=sd;
          if(wstrb!=0)begin
            if(capture_started&&((wdata&32'hfffffcff)!=32'h85))$fatal(1,"audio cleared motor/LCD GPIO: %h",wdata);
            if(!capture_mode&&((wdata&32'hc3)!=0))$fatal(1,"model-only motor direction enabled");
            gpio<=wdata[15:0];
          end
        end else if(addr==32'h03000004)begin
          v=ddr;
          if(wstrb!=0)begin
            ddr<=wdata;
            if(!wdata[5]||(wdata&32'h3c7)!=0)$fatal(1,"SD/output direction overlap: %h",wdata);
          end
        end else if(addr==32'h03000014)begin
          v=32'hffffffff;
          if(wstrb==0&&stop_frame>=0&&frame_id>=stop_frame&&!stop_sent)begin v=32'd115;stop_sent=1;end
          if(wstrb!=0)begin
            if(wdata[7:0]!=10)line={line,$sformatf("%c",wdata[7:0])};
            else begin
              $display("UART %s",line);
              if(line=="CAPTURE_TEST_BEGIN")capture_started=1;
              if(line=="TRIGGER_CAPTURE_TEST_BEGIN")capture_started=1;
              if(line.substr(0,24)=="TRIGGER_CAPTURE_TEST_FAIL")$fatal(1,"triggered capture assertion failed: %s",line);
              if(line=="TRIGGER_CAPTURE_TEST_STOP")begin
                if(!stop_sent)$fatal(1,"unexpected voice capture abort");
                $display("RV32 TRIGGER STOP PASS clocks=%0d wait=%0d",rises,wait_cycles);$finish;
              end
              if(line=="TRIGGER_CAPTURE_TEST_DONE")begin
                if(stop_sent||rises!=442368)$fatal(1,"triggered clock count %0d",rises);
                $display("RV32 TRIGGER CAPTURE PASS clocks=%0d bit_cycles=%0d..%0d wait=%0d",rises,min_period,max_period,wait_cycles);$finish;
              end
              if(line=="CAPTURE_TEST_FAIL")$fatal(1,"PCM/sign/final GPIO assertion failed");
              if(line=="CAPTURE_TEST_DONE")begin
                if(rises!=640)$fatal(1,"capture clock count %0d",rises);
                $display("RV32 CAPTURE PASS clocks=%0d bit_cycles=%0d..%0d wait=%0d",rises,min_period,max_period,wait_cycles);$finish;
              end
              if($sscanf(line,"RESULT class=%s decision=%s p_permille=%d infer_cycles=%d CR2_B=%d CR1_A=%d",label,decision,probability,infer_cycles,cr2,cr1)==6)begin
                if(label!="forward"||decision!="forward"||probability<750||cr2!=20000||cr1!=20000)$fatal(1,"RV32 model result mismatch");
              end
              if(line=="SELFTEST_DONE")begin
                if(label!="forward")$fatal(1,"missing inference result");
                $display("RV32 MODEL PASS total_cycles=%0d infer_cycles=%0d wait=%0d",cycles,infer_cycles,wait_cycles);$finish;
              end
              line="";
            end
          end
        end else if(addr>=32'h03004000&&addr<=32'h0300401c)begin
          if(wstrb!=0&&((addr==32'h03004000&&wdata!=0)||
             ((addr==32'h03004010||addr==32'h03004014||addr==32'h03004018||addr==32'h0300401c)&&wdata!=20000)))$fatal(1,"PWM enabled in model-only test");
        end else if(addr!=32'h03000010)$fatal(1,"unexpected bus address %h",addr);
        rdata<=v;
      end
    end
  end
  initial begin
    if(!$value$plusargs("IMAGE=%s",image))$fatal(1,"IMAGE missing");
    if($value$plusargs("CAPTURE=%d",capture_mode))begin end
    if($value$plusargs("WAIT=%d",wait_cycles))begin end
    if($value$plusargs("STOP_FRAME=%d",stop_frame))begin end
    $readmemh(image,flash);
    for(integer i=0;i<32768;i++)ram[i]=0;
    repeat(8)@(posedge clk);resetn<=1;
  end
endmodule
