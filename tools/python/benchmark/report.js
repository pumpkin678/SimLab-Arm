"use strict";
(() => {
    const data = JSON.parse(document.getElementById("report-data").textContent);
    const result = data.result, decision = result.evaluation.assessment;
    const byId = id => document.getElementById(id);
    const plotIds = ["position", "error", "velocity", "torque"];
    const degrees = 180 / Math.PI;
    const caseNames = {joint_sine: "关节正弦示例", posture_hold: "姿态保持", joint_step: "单轴阶跃",
        joint_tracking: "连续跟随", target_switch: "目标切换", joint_tracking_stress: "快速往返测试"};
    const caseName = id => caseNames[id] || id;
    const phaseNames = {example_tracking: "正弦跟随", initialize: "准备", multi_axis: "多轴移动",
        return_to_a: "返回基准姿态", brief_target: "短暂改目标", withdraw: "撤回目标", resume_motion: "再次移动",
        reverse: "途中反向", coupled_forward: "组合正向运动", coupled_reverse: "组合反向运动", recover: "结束回位"};
    const phaseName = id => {
        if (phaseNames[id]) return phaseNames[id];
        let match = /^pose_(\d+)_(approach|hold)$/.exec(id);
        if (match) return "姿态 " + match[1] + (match[2] === "hold" ? " · 保持" : " · 移动");
        match = /^j(\d+)_step_(\d+)$/.exec(id);
        if (match) return "J" + match[1] + " · 阶跃 " + match[2];
        match = /^j(\d+)_(tracking|recovery|slow|fast)$/.exec(id);
        if (match) return "J" + match[1] + " · " + ({tracking: "跟随", recovery: "回稳", slow: "慢速跟随", fast: "快速跟随"}[match[2]]);
        return id;
    };
    const executionNames = {completed: "正常结束", cancelled: "已取消", failed: "运行失败", not_started: "未运行", running: "运行中"};
    const names = {
        position_error_max_abs_rad: "最大位置误差", position_error_rms_rad: "位置误差 RMS",
        velocity_rms_radps: "速度 RMS", overshoot_rad: "超调", settling_time_s: "调节时间",
        tail_position_error_rms_rad: "末段位置误差 RMS", speed_peak_abs_radps: "全程峰值速度"
    };
    const reasons = {
        within_limit: "", limit_exceeded: "超出上限", band_not_held_until_end: "未稳定",
        incomplete_phase: "阶段记录不完整", metric_missing: "指标缺失", metric_unavailable: "指标不可用",
        no_target_change: "目标未变化", unsupported_phase_layout: "阶段不支持评测",
        reached: "已到达", settled: "已稳定", band_not_held: "未到达", recorded: "",
        partial_observation: "部分记录", phase_too_short: "阶段过短",
        speed_limit_exceeded: "实际速度超限", speed_limits_missing: "未记录速度上限",
        final_velocity_missing: "缺少最终速度样本", incomplete_case: "工况记录不完整", speed_data_missing: "缺少速度记录"
    };
    let ci = 0, pi = 0, ji = 0, syncing = false, generation = 0;
    const text = (tag, value, cls) => {
        const element = document.createElement(tag);
        element.textContent = value;
        if (cls) element.className = cls;
        return element;
    };
    const badge = status => text("span", status, "badge " + status);
    const display = (value, unit, precise = false) => {
        if (value === null || value === undefined) return "—";
        const scaled = value * (unit.startsWith("rad") ? degrees : 1);
        const rounded = Math.abs(scaled) < 0.00005 ? 0 : scaled;
        return (precise ? scaled.toString() : rounded.toLocaleString("zh-CN", {maximumFractionDigits: 4})) +
            (unit.startsWith("rad") ? "" : " ") + ({rad: "°", "rad/s": "°/s"}[unit] || unit);
    };
    const allChecks = [];
    for (const c of result.cases) for (const p of c.evaluation.phases)
        for (const j of p.joints) allChecks.push(...j.assessment.checks);
    for (const c of result.cases) allChecks.push(...(c.evaluation.speed?.assessment?.checks || []));
    const badChecks = allChecks.filter(c => c.status === "FAIL" || c.status === "INCOMPLETE");
    const defaultPhase = c => {
        let index = c.evaluation.phases.findIndex(p => p.assessment.status !== "OBSERVE");
        if (index < 0) index = c.phases.findIndex(p => p.kind !== "initialization");
        return Math.max(0, index);
    };
    const safe = action => (...args) => Promise.resolve().then(() => action(...args)).catch(error => {
        byId("page-error").textContent = "报告交互错误：" + error.message;
    });

    byId("overall").textContent = decision.status;
    byId("overall").className = "verdict " + decision.status;
    const passed = result.cases.filter(c => c.evaluation.assessment.status === "PASS").length;
    byId("scope").textContent = decision.teaching_cases.length ? passed + " / " + decision.teaching_cases.length + " 项通过" : "";
    byId("scope").hidden = !decision.teaching_cases.length;
    byId("execution").textContent = "运行：" + (executionNames[result.status] || result.status) + " · 规则版本 " + decision.version;
    byId("metadata").textContent = "模型：" + (result.model || "未记录") + " | 构建：" +
        (result.runtime?.configuration || "未记录") + " | 生成时间：" + new Date(data.generated_at).toLocaleString("zh-CN");
    result.cases.forEach((c, index) => {
        const card = text("button", "", "case-card");
        card.append(badge(c.evaluation.assessment.status), text("strong", caseName(c.id)));
        card.title = c.id;
        if (c.status !== "completed") card.append(text("p", executionNames[c.status] || c.status, "muted"));
        card.onclick = safe(() => choose(index, defaultPhase(c), ji));
        byId("cards").append(card);
    });
    byId("failure-count").textContent = "(" + badChecks.length + ")";
    const issues = result.cases.flatMap(c => [
        ...c.evaluation.assessment.reasons.map(reason => {
            const separator = reason.lastIndexOf(": ");
            return caseName(c.id) + "：" + (separator < 0 ? reason : phaseName(reason.slice(0, separator)) + " · " +
                (reasons[reason.slice(separator + 2)] || reason.slice(separator + 2)));
        }),
        ...(c.evaluation.assessment.missing_phases.length ? [caseName(c.id) + " 缺少阶段：" + c.evaluation.assessment.missing_phases.map(phaseName).join("、")] : [])
    ]);
    byId("failure-section").hidden = !badChecks.length && !issues.length;
    byId("failure-table").hidden = !badChecks.length;
    byId("issues").textContent = issues.join("；");
    byId("issues").hidden = !issues.length;

    function checkRow(check, failureList) {
        const row = text("tr", "", "clickable");
        const statusCell = text("td", ""); statusCell.append(badge(check.status)); row.append(statusCell);
        if (failureList) row.append(text("td", caseName(check.case_id) + " / " + phaseName(check.phase_id) + " / J" + check.joint));
        const precise = check.status === "FAIL" && check.value !== null && display(check.value, check.unit) === display(check.limit, check.unit);
        row.append(text("td", check.limit_key === "held_position_peak_rad" ? "保持偏差" : names[check.metric] || check.metric), text("td", display(check.value, check.unit, precise)),
            text("td", display(check.limit, check.unit, precise)));
        if (failureList) row.append(text("td", reasons[check.reason] ?? check.reason));
        row.onclick = safe(async () => {
            const caseIndex = result.cases.findIndex(c => c.id === check.case_id);
            const phaseIndex = result.cases[caseIndex].phases.findIndex(p => p.id === check.phase_id);
            await choose(caseIndex, phaseIndex, check.joint - 1, check);
            byId("case-select").scrollIntoView({block: "start", behavior: "smooth"});
        });
        return row;
    }
    badChecks.forEach(check => byId("failures").append(checkRow(check, true)));
    function options(element, items, selected) {
        element.replaceChildren();
        items.forEach((label, index) => { const option = text("option", label); option.value = index; element.append(option); });
        element.value = selected;
    }
    options(byId("case-select"), result.cases.map(c => caseName(c.id)), 0);
    options(byId("joint-select"), ["J1", "J2", "J3", "J4", "J5", "J6"], 0);
    byId("case-select").onchange = safe(() => {
        const index = Number(byId("case-select").value);
        return choose(index, defaultPhase(result.cases[index]), ji);
    });
    byId("phase-select").onchange = safe(() => choose(ci, Number(byId("phase-select").value), ji));
    byId("joint-select").onchange = safe(() => choose(ci, pi, Number(byId("joint-select").value)));
    byId("reset-view").onclick = safe(() => draw());

    async function choose(caseIndex, phaseIndex, jointIndex, focus = null) {
        ci = caseIndex; pi = phaseIndex; ji = jointIndex;
        byId("case-select").value = ci;
        options(byId("phase-select"), result.cases[ci].phases.map(p => phaseName(p.id)), pi);
        byId("joint-select").value = ji;
        return draw(focus);
    }

    async function draw(focus = null) {
        const token = ++generation;
        const c = result.cases[ci], phase = c.phases[pi], measured = c.evaluation.phases[pi];
        const curve = data.series[ci]?.[pi]?.[ji];
        const rows = curve?.rows || [], metrics = measured?.joints[ji];
        const speedMetrics = c.evaluation.speed?.joints?.[ji];
        const checks = [...(metrics?.assessment.checks || []),
            ...(c.evaluation.speed?.assessment?.checks || []).filter(check => check.joint === ji + 1)];
        const measurements = {...metrics, ...speedMetrics};
        byId("page-error").textContent = "";
        byId("checks").replaceChildren(...checks.map(check => checkRow(check, false)));
        byId("check-section").hidden = !checks.length;
        byId("measurement-section").open = !checks.length;
        byId("measurement-section").hidden = !metrics && !speedMetrics;
        const fields = [
            ["position_error_mean_rad", "位置误差均值", "rad"], ["position_error_rms_rad", "位置误差 RMS", "rad"],
            ["position_error_max_abs_rad", "最大位置误差", "rad"], ["velocity_rms_radps", "速度 RMS", "rad/s"],
            ["velocity_peak_abs_radps", "峰值速度", "rad/s"], ["request_torque_peak_abs_nm", "峰值请求力矩", "N·m"],
            ["applied_torque_peak_abs_nm", "峰值实际力矩", "N·m"], ["arrival_time_s", "首次到达时间", "s", "arrival_reason"],
            ["settling_time_s", "调节时间", "s", "settling_reason"], ["overshoot_rad", "超调", "rad", "overshoot_reason"],
            ["tail_position_error_mean_rad", "末段误差均值", "rad", "tail_reason"],
            ["tail_position_error_rms_rad", "末段误差 RMS", "rad", "tail_reason"],
            ["initial_position_error_rad", "起始位置误差", "rad"], ["initial_velocity_radps", "起始速度", "rad/s"],
            ["speed_peak_abs_radps", "全程峰值速度", "rad/s"],
            ["first_overspeed_time_s", "全程首次超速时间", "s"],
            ["overspeed_duration_s", "全程超速时长（按采样周期累计）", "s"]
        ];
        byId("measurements").replaceChildren(...fields.filter(([key]) => key in measurements).map(([key, label, unit, reason]) => {
            const row = text("tr", "");
            row.append(text("td", label), text("td", display(measurements[key], unit)),
                text("td", reason ? (reasons[measurements[reason]] ?? measurements[reason] ?? "") : ""));
            return row;
        }));
        byId("case-note").textContent = c.status === "failed" ? c.reason || "运行失败" : "";
        byId("case-note").hidden = c.status !== "failed";
        byId("phase-note").textContent = metrics?.held_start_in_band === false ? "本段开始时，J" + (ji + 1) + " 仍在运动或尚未到位。" : "";
        byId("phase-note").hidden = metrics?.held_start_in_band !== false;
        byId("plots").hidden = !rows.length;
        byId("no-samples").hidden = !!rows.length;
        byId("reset-view").disabled = !rows.length;
        for (const id of plotIds) if (byId(id).removeAllListeners) byId(id).removeAllListeners("plotly_relayout");
        if (!rows.length) {
            for (const id of plotIds) if (byId(id).data) Plotly.purge(byId(id));
            return;
        }
        const x = rows.map(row => row[0]), sampleIndices = rows.map(row => row[7]);
        const trace = (channel, name, color, dash, unit) => ({
            x, y: rows.map(row => row[channel]), customdata: sampleIndices, name, mode: "lines", type: "scatter",
            line: {color, width: 1.8, dash: dash || "solid"},
            hovertemplate: name + "<br>t=%{x:.6f} s<br>%{y:.6f} " + unit + "<extra></extra>"
        });
        const traces = [
            [trace(1, "目标位置", "#718298", "dash", "°"), trace(2, "实际位置", "#216bc2", null, "°")],
            [trace(3, "位置误差", "#db7a22", null, "°")],
            [trace(4, "实际速度", "#1d8b68", null, "°/s")],
            [trace(5, "请求力矩", "#718298", "dash", "N·m"), trace(6, "实际力矩", "#8d4bb6", null, "N·m")]
        ];
        const finalVelocity = c.final_velocity_sample;
        if (finalVelocity && finalVelocity.time_s >= phase.start_s && finalVelocity.time_s <= phase.end_s) {
            traces[2].push({x: [finalVelocity.time_s], y: [finalVelocity.velocity_radps[ji] * degrees],
                name: "最终速度", mode: "markers", type: "scatter", marker: {color: "#1d8b68", size: 6}});
        }
        let range = [phase.start_s, phase.end_s];
        if (focus) range = focus.time_s === null ? focus.window_s :
            [Math.max(phase.start_s, focus.time_s - 0.15), Math.min(phase.end_s, focus.time_s + 0.15)];
        const layout = (index) => {
            const shape = [], annotations = [];
            const hline = (y, color, dash) => shape.push({type: "line", xref: "paper", x0: 0, x1: 1, y0: y, y1: y, line: {color, dash, width: 1}});
            for (const p of c.phases) shape.push({type: "line", x0: p.start_s, x1: p.start_s,
                yref: "paper", y0: 0, y1: 1, line: {color: "#dbe2ec", dash: "dot", width: 1}});
            for (const [key, color, dash] of [["arrival_time_s", "#73a894", "dot"], ["settling_time_s", "#289266", "solid"]]) {
                if (metrics?.[key] !== null && metrics?.[key] !== undefined) {
                    const time = phase.start_s + metrics[key];
                    shape.push({type: "line", x0: time, x1: time, yref: "paper", y0: 0, y1: 1, line: {color, dash, width: 1}});
                }
            }
            if (index === 1) {
                for (const sign of [-1, 1]) hline(sign * result.evaluation.criteria.position_band_rad * degrees, "#97a5b7", "dot");
            }
            if (index === 2) {
                for (const sign of [-1, 1]) hline(sign * result.evaluation.criteria.velocity_band_radps * degrees, "#97a5b7", "dot");
                const speedLimit = c.speed_limits_radps?.[ji];
                if (speedLimit !== undefined) {
                    for (const sign of [-1, 1]) hline(sign * speedLimit * degrees, "#c84949", "dash");
                }
            }
            const torqueLimit = c.torque_limits_nm?.[ji];
            if (index === 3 && torqueLimit?.limited) {
                hline(torqueLimit.lower, "#c84949", "dash"); hline(torqueLimit.upper, "#c84949", "dash");
            }
            for (const check of checks) {
                if (index === 1 && check.metric === "position_error_max_abs_rad") {
                    hline(check.limit * degrees, "#c84949", "dash"); hline(-check.limit * degrees, "#c84949", "dash");
                }
                if (index === 0 && check.metric === "overshoot_rad" && metrics.response_direction) {
                    hline((metrics.response_target_rad + metrics.response_direction * check.limit) * degrees, "#c84949", "dash");
                }
                if (index === 1 && check.metric === "tail_position_error_rms_rad") shape.push({type: "rect",
                    x0: check.window_s[0], x1: check.window_s[1], yref: "paper", y0: 0, y1: 1,
                    fillcolor: check.status === "FAIL" ? "rgba(190,60,60,0.09)" : "rgba(40,100,180,0.07)", line: {width: 0}});
                if (check.metric === "settling_time_s") {
                    const cutoff = phase.start_s + check.limit;
                    shape.push({type: "line", x0: cutoff, x1: cutoff, yref: "paper", y0: 0, y1: 1,
                        line: {color: "#c84949", dash: "dash", width: 1}});
                }
            }
            if (focus) shape.push({type: "rect", x0: focus.window_s[0], x1: focus.window_s[1], yref: "paper", y0: 0, y1: 1,
                fillcolor: "rgba(219,122,34,0.05)", line: {color: "#d49854", width: 1}});
            if (index === 3 && !torqueLimit) annotations.push({xref: "paper", yref: "paper", x: 0, y: 1.12,
                text: "未记录力矩上限", showarrow: false, font: {size: 10, color: "#64768e"}});
            if (index === 3 && torqueLimit && !torqueLimit.limited) annotations.push({xref: "paper", yref: "paper", x: 0, y: 1.12,
                text: "未设置力矩上限", showarrow: false, font: {size: 10, color: "#64768e"}});
            return {title: {text: ["位置跟随", "位置误差", "关节速度", "输出力矩"][index], font: {size: 15}},
                margin: {l: 64, r: 20, t: 54, b: 45}, xaxis: {title: {text: "仿真时间 (s)"}, range: [...range], zeroline: false},
                yaxis: {title: {text: ["角度 (°)", "误差 (°)", "速度 (°/s)", "力矩 (N·m)"][index]}, zerolinecolor: "#d6dfe9"},
                hovermode: "x unified", legend: {orientation: "h", y: 1.04}, shapes: shape, annotations,
                paper_bgcolor: "white", plot_bgcolor: "white"};
        };
        await Promise.all(plotIds.map((id, index) => Plotly.react(id, traces[index], layout(index), {
            responsive: true, displaylogo: false, scrollZoom: true, modeBarButtonsToRemove: ["lasso2d", "select2d"]
        })));
        if (token !== generation) return;
        for (const id of plotIds) byId(id).on("plotly_relayout", event => {
            if (syncing) return;
            const rangeUpdate = event["xaxis.range"] || (event["xaxis.range[0]"] !== undefined ?
                [event["xaxis.range[0]"], event["xaxis.range[1]"]] : event["xaxis.autorange"] ? [phase.start_s, phase.end_s] : null);
            if (!rangeUpdate) return;
            syncing = true;
            Promise.all(plotIds.filter(other => other !== id).map(other => Plotly.relayout(other, {"xaxis.range": rangeUpdate})))
                .catch(error => { byId("page-error").textContent = error.message; }).finally(() => { syncing = false; });
        });
    }
    if (badChecks.length) {
        const first = badChecks[0];
        ci = result.cases.findIndex(c => c.id === first.case_id);
        pi = result.cases[ci].phases.findIndex(p => p.id === first.phase_id); ji = first.joint - 1;
    } else {
        ci = Math.max(0, result.cases.findIndex(c => decision.teaching_cases.includes(c.id)));
        pi = defaultPhase(result.cases[ci]);
    }
    safe(() => choose(ci, pi, ji, badChecks[0] || null))();
})();
